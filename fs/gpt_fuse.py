import os
import sys
import stat
import errno
import pyfuse3
import trio
import time
import asyncio
from pyfuse3 import FUSEError, invalidate_entry, invalidate_inode
from openai import AsyncOpenAI
from collections import defaultdict

from loguru import logger
logging_level = os.getenv("LOGGING_LEVEL", "TRACE")
logger.remove()  # Remove the default handler
logger.add(sys.stderr, level=logging_level)  # Add new handler with specified level

client = AsyncOpenAI(
    api_key=os.getenv("OPENAI_API_KEY"),
    base_url=os.getenv("OPENAI_BASE_URL"),
)

model_name = os.getenv("OPENAI_MODEL_NAME", "llama-3.3-70b-versatile")

# define node type
class NodeType:
    DIR = 0
    FILE = 1

class GPTfs(pyfuse3.Operations):
    def __init__(self):
        super().__init__()
        # last allocated inode
        self.next_inode = pyfuse3.ROOT_INODE + 1
        # store node data
        self.nodes = {}
        # store file content
        self.data = defaultdict(bytes)
        # store directory items
        self.directories = defaultdict(dict)
        # track file open status and flags
        self.open_files = {}
        # create root directory
        self.create_root()

    def create_root(self):
        """create root directory"""
        now = time.time()
        root = pyfuse3.EntryAttributes()
        root.st_ino = pyfuse3.ROOT_INODE
        root.st_mode = stat.S_IFDIR | 0o755
        root.st_nlink = 2  # . and ..
        root.st_uid = os.getuid()
        root.st_gid = os.getgid()
        root.st_size = 0
        root.st_atime_ns = int(now * 1e9)
        root.st_mtime_ns = int(now * 1e9)
        root.st_ctime_ns = int(now * 1e9)
        
        self.nodes[pyfuse3.ROOT_INODE] = (NodeType.DIR, root)
        self.directories[pyfuse3.ROOT_INODE] = {}

    async def getattr(self, inode, ctx=None):
        """get node attributes"""
        if inode not in self.nodes:
            logger.debug(f"getattr: node {inode} does not exist")
            raise FUSEError(errno.ENOENT)
        
        _, attrs = self.nodes[inode]
        logger.debug(f"getattr: return node {inode} attributes")
        return attrs

    async def lookup(self, parent_inode, name, ctx=None):
        """lookup directory item"""
        if parent_inode not in self.directories:
            logger.debug(f"lookup: parent node {parent_inode} does not exist")
            raise FUSEError(errno.ENOENT)
        
        name_str = os.fsdecode(name)  # convert bytes to str
        logger.debug(f"lookup: lookup {name_str} in {parent_inode}")
        
        if name_str not in self.directories[parent_inode]:
            logger.debug(f"lookup: {name_str} not found in {parent_inode}")
            raise FUSEError(errno.ENOENT)
        
        inode = self.directories[parent_inode][name_str]
        _, attrs = self.nodes[inode]
        
        logger.debug(f"lookup: found {name_str} in {parent_inode}, inode={inode}")
        return attrs

    async def opendir(self, inode, ctx):
        """open directory"""
        if inode not in self.nodes or self.nodes[inode][0] != NodeType.DIR:
            logger.debug(f"opendir: {inode} is not a directory")
            raise FUSEError(errno.ENOTDIR)
        
        logger.debug(f"opendir: open directory {inode}")
        return inode

    async def readdir(self, inode, off, token):
        """read directory content"""
        if inode not in self.nodes or self.nodes[inode][0] != NodeType.DIR:
            logger.debug(f"readdir: {inode} is not a directory")
            raise FUSEError(errno.ENOTDIR)
        
        entries = list(self.directories[inode].items())
        entries.sort() 
        
        logger.debug(f"readdir: read directory {inode}, starting from offset {off}, {len(entries)} entries")
        
        # return directory entries starting from offset
        for i, (name, child_inode) in enumerate(entries[off:], off):
            name_bytes = os.fsencode(name)  # convert str to bytes
            attr = await self.getattr(child_inode)  # get attributes
            if not pyfuse3.readdir_reply(token, name_bytes, attr, i+1):
                logger.warning(f"readdir: failed to reply to readdir request, token={token}, name={name}, child_inode={child_inode}, i={i}")
                break
        
        return

    async def mkdir(self, parent_inode, name, mode, ctx):
        """create directory (create a new conversation session)"""
        if parent_inode != pyfuse3.ROOT_INODE:
            logger.debug(f"mkdir: try to create directory under non-root directory {parent_inode}, which is not supported")
            raise FUSEError(errno.EPERM)  # only allow creating session directory under root
        
        name_str = os.fsdecode(name)  # convert bytes to str
        if name_str in self.directories[parent_inode]:
            logger.debug(f"mkdir: directory {name_str} already exists")
            raise FUSEError(errno.EEXIST)
        
        # create session directory
        inode = self._create_node(NodeType.DIR, mode)
        self.directories[parent_inode][name_str] = inode
        self.directories[inode] = {}
        
        # create input, output and error files in session directory
        try:
            self._create_session_files(inode)
            logger.debug(f"mkdir: create session directory {name_str}, inode={inode}, and created session files")
        except Exception as e:
            logger.error(f"failed to create session files: {e}", exc_info=True)
            raise
        
        return await self.getattr(inode)

    def _create_session_files(self, session_inode):
        """create necessary files in session directory"""
        # create input file
        input_inode = self._create_node(NodeType.FILE, stat.S_IFREG | 0o644)
        self.directories[session_inode]['input'] = input_inode
        logger.debug(f"created input file in session {session_inode}, inode={input_inode}")
        
        # create output file
        output_inode = self._create_node(NodeType.FILE, stat.S_IFREG | 0o644)
        self.directories[session_inode]['output'] = output_inode
        logger.debug(f"created output file in session {session_inode}, inode={output_inode}")
        
        # create error file
        error_inode = self._create_node(NodeType.FILE, stat.S_IFREG | 0o644)
        self.directories[session_inode]['error'] = error_inode
        logger.debug(f"created error file in session {session_inode}, inode={error_inode}")

    def _create_node(self, type_, mode):
        """create a new node"""
        inode = self.next_inode
        self.next_inode += 1
        
        now = time.time()
        entry = pyfuse3.EntryAttributes()
        entry.st_ino = inode
        entry.st_mode = mode
        entry.st_nlink = 1
        entry.st_uid = os.getuid()
        entry.st_gid = os.getgid()
        entry.st_size = 0
        entry.st_atime_ns = int(now * 1e9)
        entry.st_mtime_ns = int(now * 1e9)
        entry.st_ctime_ns = int(now * 1e9)
        
        self.nodes[inode] = (type_, entry)
        return inode

    async def open(self, inode, flags, ctx):
        """open file"""
        if inode not in self.nodes:
            logger.debug(f"open: node {inode} does not exist")
            raise FUSEError(errno.ENOENT)
        
        node_type, _ = self.nodes[inode]
        if node_type != NodeType.FILE:
            logger.debug(f"open: node {inode} is not a file")
            raise FUSEError(errno.EISDIR)
        
        # track file open flags
        open_flags = flags
        self.open_files[inode] = {
            'flags': open_flags,
            'truncated': False,
            'direct_io': False,  # default not use direct IO
            'keep_cache': False  # default not keep cache
        }
        
        # check if truncate file
        if flags & os.O_TRUNC:
            logger.debug(f"open: truncate file {inode}")
            self.data[inode] = b''
            self.nodes[inode][1].st_size = 0
            self.nodes[inode][1].st_mtime_ns = int(time.time() * 1e9)
            self.open_files[inode]['truncated'] = True
        
        logger.debug(f"open: open file {inode}, flags: {flags}")
        
        # for output file, use direct IO to avoid cache problem
        if self._is_output_file(inode):
            logger.debug(f"open: file {inode} is output file, use direct IO")
            self.open_files[inode]['direct_io'] = True
            return pyfuse3.FileInfo(fh=inode, direct_io=True)
        
        return pyfuse3.FileInfo(fh=inode)

    async def release(self, fh):
        """close file"""
        if fh in self.open_files:
            logger.debug(f"release: close file {fh}")
            del self.open_files[fh]
        return

    async def read(self, fh, off, size):
        """read file content"""
        if fh not in self.nodes or self.nodes[fh][0] != NodeType.FILE:
            logger.debug(f"read: invalid file handle {fh}")
            raise FUSEError(errno.EINVAL)
        
        data = self.data[fh]
        logger.debug(f"read: read {size} bytes from file {fh}, offset {off}, actual readable {len(data) - off if off < len(data) else 0} bytes")
        return data[off:off+size]

    async def write(self, fh, off, buf):
        """write file content"""
        if fh not in self.nodes or self.nodes[fh][0] != NodeType.FILE:
            logger.debug(f"write: invalid file handle {fh}")
            raise FUSEError(errno.EINVAL)
        
        # update file content
        data = self.data[fh]
        
        # if this is the first write and the file should be truncated, ignore the offset
        if fh in self.open_files and self.open_files[fh].get('truncated', False):
            logger.debug(f"write: file {fh} has been truncated, write directly")
            self.data[fh] = buf
            self.open_files[fh]['truncated'] = False  # reset truncated flag
        else:
            # normal write
            if off + len(buf) > len(data):
                data = data[:off] + buf
            else:
                data = data[:off] + buf + data[off+len(buf):]
            self.data[fh] = data
        
        # update file size
        self.nodes[fh][1].st_size = len(self.data[fh])
        self.nodes[fh][1].st_mtime_ns = int(time.time() * 1e9)
        
        logger.debug(f"write: write {len(buf)} bytes to file {fh}, offset {off}, new size: {len(self.data[fh])}")
        
        # if this is input file, process GPT request
        try:
            if self._is_input_file(fh):
                logger.debug(f"write: detected writing to input file, prepare to trigger GPT request")
                session_inode = self._find_session_inode(fh)
                if session_inode:
                    # use background task to process request, avoid blocking write call
                    trio.lowlevel.spawn_system_task(self._process_gpt_request, session_inode)
                    logger.debug(f"write: triggered GPT request processing in background")
        except Exception as e:
            logger.error(f"failed to trigger GPT request: {e}", exc_info=True)
            # do not raise exception, write operation itself still succeeds
        
        return len(buf)
    
    def _is_input_file(self, inode):
        """check if this is input file"""
        # traverse directory to find input file
        for dir_inode, entries in self.directories.items():
            if 'input' in entries and entries['input'] == inode:
                return True
        return False
    
    def _is_output_file(self, inode):
        """check if this is output file"""
        # traverse directory to find output file
        for dir_inode, entries in self.directories.items():
            if 'output' in entries and entries['output'] == inode:
                return True
        return False
    
    def _find_session_inode(self, input_inode):
        """find session directory inode according to input file inode"""
        for session_inode, entries in self.directories.items():
            if 'input' in entries and entries['input'] == input_inode:
                return session_inode
        return None
    
    async def _invalidate_cache(self, inode):
        """invalidate cache for specified inode"""
        try:
            # check if inode is valid
            if inode not in self.nodes:
                logger.debug(f"_invalidate_cache: skipping invalidation for non-existent inode {inode}")
                return
                
            logger.debug(f"_invalidate_cache: invalidate cache for node {inode}")
            # use try/except to handle possible errors
            try:
                result = pyfuse3.invalidate_inode(inode)
                if result is not None:  # only await when return value is not None
                    await result
            except FileNotFoundError:
                logger.debug(f"_invalidate_cache: inode {inode} not found in FUSE layer, ignoring")
            except Exception as e:
                logger.warning(f"_invalidate_cache: non-critical error invalidating inode {inode}: {e}")
        except Exception as e:
            logger.error(f"_invalidate_cache: failed to invalidate cache: {e}", exc_info=True)

    async def _process_gpt_request(self, session_inode):
        """process GPT request"""
        try:
            logger.debug(f"_process_gpt_request: start processing GPT request for session {session_inode}")
            entries = self.directories[session_inode]
            input_inode = entries['input']
            output_inode = entries['output']
            error_inode = entries['error']
            
            # get user input
            prompt = self.data[input_inode].decode('utf-8', errors='replace')
            logger.debug(f"_process_gpt_request: user input: {prompt[:50]}...")
            
            try:
                # call OpenAI API
                logger.debug("_process_gpt_request: calling OpenAI API...")
                response = await client.chat.completions.create(
                    model=model_name,
                    messages=[
                        {"role": "user", "content": prompt}
                    ]
                )
                
                # get reply text
                gpt_response = response.choices[0].message.content
                logger.debug(f"_process_gpt_request: got API response: {gpt_response[:50]}...")
                
                # write to output file
                self.data[output_inode] = gpt_response.encode('utf-8')
                self.nodes[output_inode][1].st_size = len(self.data[output_inode])
                self.nodes[output_inode][1].st_mtime_ns = int(time.time() * 1e9)
                logger.debug("_process_gpt_request: updated output file")
                
                # clear error file
                self.data[error_inode] = b''
                self.nodes[error_inode][1].st_size = 0
                self.nodes[error_inode][1].st_mtime_ns = int(time.time() * 1e9)
                
                # invalidate cache, ensure new content can be read immediately
                await self._invalidate_cache(output_inode)
                await self._invalidate_cache(error_inode)
                
                # find session directory name, for logging
                session_name = "unknown session"
                for name, inode in self.directories[pyfuse3.ROOT_INODE].items():
                    if inode == session_inode:
                        session_name = name
                        break
                
                logger.info(f"GPT request processed successfully, session: {session_name}")
                
            except Exception as e:
                # write to error file
                error_msg = f"Error: {str(e)}"
                logger.error(f"_process_gpt_request: failed to process GPT request: {e}", exc_info=True)
                self.data[error_inode] = error_msg.encode('utf-8')
                self.nodes[error_inode][1].st_size = len(self.data[error_inode])
                self.nodes[error_inode][1].st_mtime_ns = int(time.time() * 1e9)
                
                # invalidate cache for error file
                await self._invalidate_cache(error_inode)
        except Exception as e:
            logger.error(f"_process_gpt_request: failed to process GPT request: {e}", exc_info=True)

    async def setattr(self, inode, attr, fields, fh, ctx):
        """set file attributes"""
        if inode not in self.nodes:
            logger.debug(f"setattr: node {inode} does not exist")
            raise FUSEError(errno.ENOENT)
        
        node_type, attrs = self.nodes[inode]
        
        if fields.update_size:
            if node_type == NodeType.DIR:
                logger.debug(f"setattr: try to set size for directory {inode}")
                raise FUSEError(errno.EISDIR)
            
            # handle file truncation
            old_size = attrs.st_size
            attrs.st_size = attr.st_size
            
            if attr.st_size < old_size:
                # truncate file
                logger.debug(f"setattr: truncate file {inode} from {old_size} to {attr.st_size}")
                self.data[inode] = self.data[inode][:attr.st_size]
            elif attr.st_size > old_size:
                # extend file
                logger.debug(f"setattr: extend file {inode} from {old_size} to {attr.st_size}")
                self.data[inode] = self.data[inode] + b'\0' * (attr.st_size - old_size)
            
            # if file is truncated to 0, mark as truncated
            if attr.st_size == 0 and inode in self.open_files:
                self.open_files[inode]['truncated'] = True
                logger.debug(f"setattr: file {inode} is truncated to 0, marked as truncated")
        
        # handle other attribute updates
        if fields.update_mode:
            attrs.st_mode = (attrs.st_mode & ~0o777) | (attr.st_mode & 0o777)
            logger.debug(f"setattr: updated file {inode} mode to {attrs.st_mode}")
        
        if fields.update_uid:
            attrs.st_uid = attr.st_uid
            logger.debug(f"setattr: updated file {inode} uid to {attr.st_uid}")
        
        if fields.update_gid:
            attrs.st_gid = attr.st_gid
            logger.debug(f"setattr: updated file {inode} gid to {attr.st_gid}")
        
        if fields.update_atime:
            attrs.st_atime_ns = attr.st_atime_ns
            logger.debug(f"setattr: updated file {inode} atime")
        
        if fields.update_mtime:
            attrs.st_mtime_ns = attr.st_mtime_ns
            logger.debug(f"setattr: updated file {inode} mtime")
        
        return attrs

    def _is_session_dir(self, inode):
        """check if this is session directory"""
        # check if this is a subdirectory of root directory
        for name, child_inode in self.directories[pyfuse3.ROOT_INODE].items():
            if child_inode == inode:
                return True
        return False

async def main(mountpoint):
    """main function"""
    logger.info(f"prepare to mount GPT file system to {mountpoint}")
    gptfs = GPTfs()
    fuse_options = set(pyfuse3.default_options)
    fuse_options.add('fsname=gptfs')
    fuse_options.discard('default_permissions')
    
    try:
        logger.info("initialize FUSE")
        pyfuse3.init(gptfs, mountpoint, fuse_options)
        logger.info("start FUSE main loop")
        await pyfuse3.main()
    except Exception as e:
        logger.error(f"error in FUSE main loop: {e}", exc_info=True)
        raise
    finally:
        logger.info("close FUSE")
        pyfuse3.close()

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <mount point>")
        sys.exit(1)
    
    mountpoint = sys.argv[1]
    if not os.path.isdir(mountpoint):
        print(f"Error: mount point {mountpoint} does not exist or is not a directory")
        sys.exit(1)
    
    logger.info(f"start to mount GPT file system to {mountpoint}")
    try:
        trio.run(main, mountpoint)
    except KeyboardInterrupt:
        logger.info("received interrupt signal, unmounting file system...")
    except Exception as e:
        logger.error(f"error: {e}", exc_info=True)
        sys.exit(1)
    finally:
        logger.info("GPT file system has been unmounted")
