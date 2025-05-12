import os
import sys
import stat
import errno
import pyfuse3
import trio
import time
import logging
import asyncio
from pyfuse3 import FUSEError
from openai import AsyncOpenAI
from collections import defaultdict

# 配置日志
logging.basicConfig(level=logging.DEBUG)  # 提高日志级别以获取更多信息
logger = logging.getLogger(__name__)

# 初始化OpenAI客户端
client = AsyncOpenAI(
    api_key=os.getenv("OPENAI_API_KEY"),
    base_url=os.getenv("OPENAI_BASE_URL"),
)

model_name = os.getenv("OPENAI_MODEL_NAME", "gpt-3.5-turbo")

# 定义文件系统中的节点类型
class NodeType:
    DIR = 0
    FILE = 1

class GPTfs(pyfuse3.Operations):
    def __init__(self):
        super().__init__()
        # 最后一个已分配的inode
        self.next_inode = pyfuse3.ROOT_INODE + 1
        # 存储节点数据
        self.nodes = {}
        # 存储文件内容
        self.data = defaultdict(bytes)
        # 存储目录项
        self.directories = defaultdict(dict)
        # 跟踪文件打开状态和标志
        self.open_files = {}
        # 创建根目录
        self.create_root()

    def create_root(self):
        """创建根目录"""
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
        """获取节点属性"""
        if inode not in self.nodes:
            logger.debug(f"getattr: 节点 {inode} 不存在")
            raise FUSEError(errno.ENOENT)
        
        _, attrs = self.nodes[inode]
        logger.debug(f"getattr: 返回节点 {inode} 的属性")
        return attrs

    async def lookup(self, parent_inode, name, ctx=None):
        """查找目录项"""
        if parent_inode not in self.directories:
            logger.debug(f"lookup: 父节点 {parent_inode} 不存在")
            raise FUSEError(errno.ENOENT)
        
        name_str = os.fsdecode(name)  # 将bytes转换为str
        logger.debug(f"lookup: 在 {parent_inode} 中查找 {name_str}")
        
        if name_str not in self.directories[parent_inode]:
            logger.debug(f"lookup: 在 {parent_inode} 中未找到 {name_str}")
            raise FUSEError(errno.ENOENT)
        
        inode = self.directories[parent_inode][name_str]
        _, attrs = self.nodes[inode]
        
        logger.debug(f"lookup: 在 {parent_inode} 中找到 {name_str}，inode={inode}")
        return attrs

    async def opendir(self, inode, ctx):
        """打开目录"""
        if inode not in self.nodes or self.nodes[inode][0] != NodeType.DIR:
            logger.debug(f"opendir: {inode} 不是目录")
            raise FUSEError(errno.ENOTDIR)
        
        logger.debug(f"opendir: 打开目录 {inode}")
        return inode

    async def readdir(self, inode, off, token):
        """读取目录内容"""
        if inode not in self.nodes or self.nodes[inode][0] != NodeType.DIR:
            logger.debug(f"readdir: {inode} 不是目录")
            raise FUSEError(errno.ENOTDIR)
        
        entries = list(self.directories[inode].items())
        entries.sort() 
        
        logger.debug(f"readdir: 读取目录 {inode}，从偏移量 {off} 开始，共有 {len(entries)} 个条目")
        
        # 从off开始返回目录项
        for i, (name, child_inode) in enumerate(entries[off:], off):
            name_bytes = os.fsencode(name)  # 将str转换为bytes
            attr = await self.getattr(child_inode)  # 获取属性
            if not pyfuse3.readdir_reply(token, name_bytes, attr, i+1):
                break
        
        return

    async def mkdir(self, parent_inode, name, mode, ctx):
        """创建目录（创建一个新的对话session）"""
        if parent_inode != pyfuse3.ROOT_INODE:
            logger.debug(f"mkdir: 尝试在非根目录 {parent_inode} 下创建目录")
            raise FUSEError(errno.EPERM)  # 只允许在根目录下创建会话目录
        
        name_str = os.fsdecode(name)  # 将bytes转换为str
        if name_str in self.directories[parent_inode]:
            logger.debug(f"mkdir: 目录 {name_str} 已存在")
            raise FUSEError(errno.EEXIST)
        
        # 创建会话目录
        inode = self._create_node(NodeType.DIR, mode)
        self.directories[parent_inode][name_str] = inode
        self.directories[inode] = {}
        
        # 在会话目录中创建input、output和error文件
        try:
            self._create_session_files(inode)
            logger.debug(f"mkdir: 创建会话目录 {name_str}，inode={inode}，并创建了会话文件")
        except Exception as e:
            logger.error(f"创建会话文件失败: {e}", exc_info=True)
            raise
        
        return await self.getattr(inode)

    def _create_session_files(self, session_inode):
        """在会话目录中创建必要的文件"""
        # 创建input文件
        input_inode = self._create_node(NodeType.FILE, stat.S_IFREG | 0o644)
        self.directories[session_inode]['input'] = input_inode
        logger.debug(f"已在会话 {session_inode} 中创建input文件，inode={input_inode}")
        
        # 创建output文件
        output_inode = self._create_node(NodeType.FILE, stat.S_IFREG | 0o644)
        self.directories[session_inode]['output'] = output_inode
        logger.debug(f"已在会话 {session_inode} 中创建output文件，inode={output_inode}")
        
        # 创建error文件
        error_inode = self._create_node(NodeType.FILE, stat.S_IFREG | 0o644)
        self.directories[session_inode]['error'] = error_inode
        logger.debug(f"已在会话 {session_inode} 中创建error文件，inode={error_inode}")

    def _create_node(self, type_, mode):
        """创建一个新节点"""
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
        """打开文件"""
        if inode not in self.nodes:
            logger.debug(f"open: 节点 {inode} 不存在")
            raise FUSEError(errno.ENOENT)
        
        node_type, _ = self.nodes[inode]
        if node_type != NodeType.FILE:
            logger.debug(f"open: 节点 {inode} 不是文件")
            raise FUSEError(errno.EISDIR)
        
        # 跟踪文件打开标志
        open_flags = flags
        self.open_files[inode] = {
            'flags': open_flags,
            'truncated': False
        }
        
        # 检查是否需要截断文件
        if flags & os.O_TRUNC:
            logger.debug(f"open: 截断文件 {inode}")
            self.data[inode] = b''
            self.nodes[inode][1].st_size = 0
            self.nodes[inode][1].st_mtime_ns = int(time.time() * 1e9)
            self.open_files[inode]['truncated'] = True
        
        logger.debug(f"open: 打开文件 {inode}，标志: {flags}")
        return pyfuse3.FileInfo(fh=inode)

    async def release(self, fh):
        """关闭文件"""
        if fh in self.open_files:
            logger.debug(f"release: 关闭文件 {fh}")
            del self.open_files[fh]
        return

    async def read(self, fh, off, size):
        """读取文件内容"""
        if fh not in self.nodes or self.nodes[fh][0] != NodeType.FILE:
            logger.debug(f"read: 文件句柄 {fh} 无效")
            raise FUSEError(errno.EINVAL)
        
        data = self.data[fh]
        logger.debug(f"read: 从文件 {fh} 的偏移量 {off} 读取 {size} 字节，实际可读 {len(data) - off if off < len(data) else 0} 字节")
        return data[off:off+size]

    async def write(self, fh, off, buf):
        """写入文件内容"""
        if fh not in self.nodes or self.nodes[fh][0] != NodeType.FILE:
            logger.debug(f"write: 文件句柄 {fh} 无效")
            raise FUSEError(errno.EINVAL)
        
        # 更新文件内容
        data = self.data[fh]
        
        # 如果这是第一次写入，且文件应该被截断，则忽略偏移量
        if fh in self.open_files and self.open_files[fh].get('truncated', False):
            logger.debug(f"write: 文件 {fh} 已被截断，直接写入")
            self.data[fh] = buf
            self.open_files[fh]['truncated'] = False  # 重置截断标志
        else:
            # 正常写入
            if off + len(buf) > len(data):
                data = data[:off] + buf
            else:
                data = data[:off] + buf + data[off+len(buf):]
            self.data[fh] = data
        
        # 更新文件大小
        self.nodes[fh][1].st_size = len(self.data[fh])
        self.nodes[fh][1].st_mtime_ns = int(time.time() * 1e9)
        
        logger.debug(f"write: 向文件 {fh} 的偏移量 {off} 写入 {len(buf)} 字节，新大小: {len(self.data[fh])}")
        
        # 如果是input文件，处理GPT请求
        try:
            if self._is_input_file(fh):
                logger.debug(f"write: 检测到写入input文件，准备触发GPT请求")
                session_inode = self._find_session_inode(fh)
                if session_inode:
                    # 使用后台任务处理请求，避免阻塞write调用
                    trio.lowlevel.spawn_system_task(self._process_gpt_request, session_inode)
                    logger.debug(f"write: 已在后台触发GPT请求处理")
        except Exception as e:
            logger.error(f"触发GPT请求失败: {e}", exc_info=True)
            # 不抛出异常，write操作本身仍然成功
        
        return len(buf)
    
    def _is_input_file(self, inode):
        """判断是否是input文件"""
        # 遍历目录查找input文件
        for dir_inode, entries in self.directories.items():
            if 'input' in entries and entries['input'] == inode:
                return True
        return False
    
    def _find_session_inode(self, input_inode):
        """根据input文件的inode找到对应的session目录inode"""
        for session_inode, entries in self.directories.items():
            if 'input' in entries and entries['input'] == input_inode:
                return session_inode
        return None

    async def _process_gpt_request(self, session_inode):
        """处理GPT请求"""
        try:
            logger.debug(f"_process_gpt_request: 开始处理会话 {session_inode} 的GPT请求")
            entries = self.directories[session_inode]
            input_inode = entries['input']
            output_inode = entries['output']
            error_inode = entries['error']
            
            # 获取用户输入
            prompt = self.data[input_inode].decode('utf-8', errors='replace')
            logger.debug(f"_process_gpt_request: 用户输入: {prompt[:50]}...")
            
            try:
                # 调用GPT API
                logger.debug("_process_gpt_request: 调用OpenAI API...")
                response = await client.chat.completions.create(
                    model=model_name,
                    messages=[
                        {"role": "user", "content": prompt}
                    ]
                )
                
                # 获取回复文本
                gpt_response = response.choices[0].message.content
                logger.debug(f"_process_gpt_request: 获取到API响应: {gpt_response[:50]}...")
                
                # 写入output文件
                self.data[output_inode] = gpt_response.encode('utf-8')
                self.nodes[output_inode][1].st_size = len(self.data[output_inode])
                self.nodes[output_inode][1].st_mtime_ns = int(time.time() * 1e9)
                logger.debug("_process_gpt_request: 已更新output文件")
                
                # 清空error文件
                self.data[error_inode] = b''
                self.nodes[error_inode][1].st_size = 0
                self.nodes[error_inode][1].st_mtime_ns = int(time.time() * 1e9)
                
                logger.info(f"GPT请求处理成功")
                
            except Exception as e:
                # 写入error文件
                error_msg = f"Error: {str(e)}"
                logger.error(f"_process_gpt_request: 处理GPT请求失败: {e}", exc_info=True)
                self.data[error_inode] = error_msg.encode('utf-8')
                self.nodes[error_inode][1].st_size = len(self.data[error_inode])
                self.nodes[error_inode][1].st_mtime_ns = int(time.time() * 1e9)
        except Exception as e:
            logger.error(f"_process_gpt_request: 处理过程中发生未捕获的异常: {e}", exc_info=True)

    async def setattr(self, inode, attr, fields, fh, ctx):
        """设置文件属性"""
        if inode not in self.nodes:
            logger.debug(f"setattr: 节点 {inode} 不存在")
            raise FUSEError(errno.ENOENT)
        
        node_type, attrs = self.nodes[inode]
        
        if fields.update_size:
            if node_type == NodeType.DIR:
                logger.debug(f"setattr: 尝试设置目录 {inode} 的大小")
                raise FUSEError(errno.EISDIR)
            
            # 处理文件截断
            old_size = attrs.st_size
            attrs.st_size = attr.st_size
            
            if attr.st_size < old_size:
                # 截断文件
                logger.debug(f"setattr: 截断文件 {inode} 从 {old_size} 到 {attr.st_size}")
                self.data[inode] = self.data[inode][:attr.st_size]
            elif attr.st_size > old_size:
                # 扩展文件
                logger.debug(f"setattr: 扩展文件 {inode} 从 {old_size} 到 {attr.st_size}")
                self.data[inode] = self.data[inode] + b'\0' * (attr.st_size - old_size)
            
            # 如果文件被截断为0，标记为已截断
            if attr.st_size == 0 and inode in self.open_files:
                self.open_files[inode]['truncated'] = True
                logger.debug(f"setattr: 文件 {inode} 被截断为0，标记为已截断")
        
        # 处理其他属性的更新
        if fields.update_mode:
            attrs.st_mode = (attrs.st_mode & ~0o777) | (attr.st_mode & 0o777)
            logger.debug(f"setattr: 更新文件 {inode} 的模式为 {attrs.st_mode}")
        
        if fields.update_uid:
            attrs.st_uid = attr.st_uid
            logger.debug(f"setattr: 更新文件 {inode} 的uid为 {attr.st_uid}")
        
        if fields.update_gid:
            attrs.st_gid = attr.st_gid
            logger.debug(f"setattr: 更新文件 {inode} 的gid为 {attr.st_gid}")
        
        if fields.update_atime:
            attrs.st_atime_ns = attr.st_atime_ns
            logger.debug(f"setattr: 更新文件 {inode} 的atime")
        
        if fields.update_mtime:
            attrs.st_mtime_ns = attr.st_mtime_ns
            logger.debug(f"setattr: 更新文件 {inode} 的mtime")
        
        return attrs

    async def unlink(self, parent_inode, name, ctx):
        """删除文件"""
        if parent_inode not in self.directories:
            logger.debug(f"unlink: 父节点 {parent_inode} 不存在")
            raise FUSEError(errno.ENOENT)
        
        name_str = os.fsdecode(name)  # 将bytes转换为str
        if name_str not in self.directories[parent_inode]:
            logger.debug(f"unlink: 在目录 {parent_inode} 中未找到 {name_str}")
            raise FUSEError(errno.ENOENT)
        
        inode = self.directories[parent_inode][name_str]
        if self.nodes[inode][0] == NodeType.DIR:
            logger.debug(f"unlink: 尝试删除目录 {inode}，但使用了unlink")
            raise FUSEError(errno.EISDIR)
        
        # 禁止删除session中的特殊文件
        if name_str in ['input', 'output', 'error'] and self._is_session_dir(parent_inode):
            logger.debug(f"unlink: 尝试删除会话中的特殊文件 {name_str}")
            raise FUSEError(errno.EPERM)
        
        logger.debug(f"unlink: 删除文件 {name_str}，inode={inode}")
        del self.directories[parent_inode][name_str]
        del self.nodes[inode]
        if inode in self.data:
            del self.data[inode]
        if inode in self.open_files:
            del self.open_files[inode]

    def _is_session_dir(self, inode):
        """判断是否是会话目录"""
        # 检查是否是根目录的子目录
        for name, child_inode in self.directories[pyfuse3.ROOT_INODE].items():
            if child_inode == inode:
                return True
        return False

    async def rmdir(self, parent_inode, name, ctx):
        """删除目录"""
        if parent_inode not in self.directories:
            logger.debug(f"rmdir: 父节点 {parent_inode} 不存在")
            raise FUSEError(errno.ENOENT)
        
        name_str = os.fsdecode(name)  # 将bytes转换为str
        if name_str not in self.directories[parent_inode]:
            logger.debug(f"rmdir: 在目录 {parent_inode} 中未找到 {name_str}")
            raise FUSEError(errno.ENOENT)
        
        inode = self.directories[parent_inode][name_str]
        if self.nodes[inode][0] != NodeType.DIR:
            logger.debug(f"rmdir: {inode} 不是目录")
            raise FUSEError(errno.ENOTDIR)
        
        if self.directories[inode]:
            logger.debug(f"rmdir: 目录 {inode} 不为空")
            raise FUSEError(errno.ENOTEMPTY)
        
        logger.debug(f"rmdir: 删除目录 {name_str}，inode={inode}")
        del self.directories[parent_inode][name_str]
        del self.nodes[inode]
        del self.directories[inode]

async def main(mountpoint):
    """主函数"""
    logger.info(f"准备挂载GPT文件系统到 {mountpoint}")
    gptfs = GPTfs()
    fuse_options = set(pyfuse3.default_options)
    fuse_options.add('fsname=gptfs')
    fuse_options.discard('default_permissions')
    
    try:
        logger.info("初始化FUSE")
        pyfuse3.init(gptfs, mountpoint, fuse_options)
        logger.info("启动FUSE主循环")
        await pyfuse3.main()
    except Exception as e:
        logger.error(f"FUSE主循环中发生错误: {e}", exc_info=True)
        raise
    finally:
        logger.info("关闭FUSE")
        pyfuse3.close()

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"用法: {sys.argv[0]} <挂载点>")
        sys.exit(1)
    
    mountpoint = sys.argv[1]
    if not os.path.isdir(mountpoint):
        print(f"错误: 挂载点 {mountpoint} 不存在或不是目录")
        sys.exit(1)
    
    logging.info(f"开始挂载GPT文件系统到 {mountpoint}")
    try:
        trio.run(main, mountpoint)
    except KeyboardInterrupt:
        logging.info("收到中断信号，正在卸载文件系统...")
    except Exception as e:
        logging.error(f"发生错误: {e}", exc_info=True)
        sys.exit(1)
    finally:
        logging.info("GPT文件系统已卸载")
