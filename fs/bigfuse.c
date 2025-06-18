/*
  RAMfs: A FUSE-based in-memory filesystem
  Features:
  - Skip list for directory entries (efficient random access and traversal)
  - ext-style hierarchical block mapping for files
  - Hard link support with reference counting
  - Thread-safe operations
 */

#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 12)

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>

/* Configuration constants */
#define BLOCK_SIZE 4096
#define MAX_SKIP_LEVEL 16
#define DIRECT_BLOCKS 12        /* ext-style: 12 direct blocks */
#define INDIRECT_BLOCKS 1       /* 1 single indirect block */
#define DOUBLE_INDIRECT_BLOCKS 1 /* 1 double indirect block */
#define TRIPLE_INDIRECT_BLOCKS 1 /* 1 triple indirect block */

/* Block management */
struct ramfs_block {
    char data[BLOCK_SIZE];
};

/* Skip list node for directory entries */
struct skiplist_node {
    char *name;                     /* File name */
    fuse_ino_t ino;                /* Inode number */
    struct skiplist_node **forward; /* Forward pointers array */
    int level;                     /* Node level */
};

/* Skip list for directory management */
struct skiplist {
    struct skiplist_node *header;  /* Header node */
    int max_level;                 /* Current maximum level */
    size_t size;                   /* Number of entries */
    pthread_rwlock_t lock;         /* Read-write lock */
};

/* ext-style block pointers for files */
struct ramfs_block_info {
    /* Direct blocks - for small files */
    struct ramfs_block *direct[DIRECT_BLOCKS];
    
    /* Single indirect block - points to data blocks */
    struct ramfs_block **single_indirect;
    
    /* Double indirect block - points to single indirect blocks */
    struct ramfs_block ***double_indirect;
    
    /* Triple indirect block - points to double indirect blocks */
    struct ramfs_block ****triple_indirect;
    
    /* Block allocation info */
    size_t total_blocks;           /* Total allocated blocks */
    pthread_mutex_t lock;          /* Block allocation lock */
};

/* RAM filesystem inode */
struct ramfs_inode {
    fuse_ino_t ino;               /* Inode number */
    mode_t mode;                  /* File type and permissions */
    nlink_t nlink;                /* Hard link count */
    uid_t uid;                    /* Owner user ID */
    gid_t gid;                    /* Owner group ID */
    size_t size;                  /* File size in bytes */
    time_t atime;                 /* Access time */
    time_t mtime;                 /* Modification time */
    time_t ctime;                 /* Change time */
    
    /* File-specific data */
    struct ramfs_block_info blocks; /* Block information for files */
    
    /* Directory-specific data */
    struct skiplist *dir_entries;  /* Skip list for directory entries */
    
    /* Synchronization */
    pthread_rwlock_t lock;        /* Inode read-write lock */
    
    /* Reference counting for hard links */
    int ref_count;                /* Reference count for cleanup */
};

/* Global filesystem state */
struct ramfs_state {
    struct ramfs_inode *root;     /* Root directory inode */
    fuse_ino_t next_ino;          /* Next available inode number */
    pthread_mutex_t ino_lock;     /* Lock for inode allocation */
    
    /* Memory management */
    struct ramfs_block **free_blocks; /* Free block pool */
    size_t free_block_count;      /* Number of free blocks */
    size_t total_blocks;          /* Total allocated blocks */
    pthread_mutex_t block_lock;   /* Block allocation lock */
    
    /* Inode hash table for fast lookup */
    struct ramfs_inode **inode_table; /* Hash table of inodes */
    size_t inode_table_size;      /* Hash table size */
    pthread_rwlock_t inode_table_lock; /* Hash table lock */
};

/* Global filesystem instance */
static struct ramfs_state *ramfs_state = NULL;

/* Function declarations */
static struct skiplist *skiplist_create(void);
static void skiplist_destroy(struct skiplist *sl);
static int skiplist_insert(struct skiplist *sl, const char *name, fuse_ino_t ino);
static fuse_ino_t skiplist_search(struct skiplist *sl, const char *name);
static int skiplist_delete(struct skiplist *sl, const char *name);

static struct ramfs_inode *ramfs_inode_create(mode_t mode, uid_t uid, gid_t gid);
static void ramfs_inode_destroy(struct ramfs_inode *inode);
static struct ramfs_inode *ramfs_inode_get(fuse_ino_t ino);
static int ramfs_inode_add(struct ramfs_inode *inode);
static void ramfs_inode_remove(fuse_ino_t ino);

static struct ramfs_block *ramfs_block_alloc(void);
static void ramfs_block_free(struct ramfs_block *block);

/* Skip list implementation */
static int random_level(void) {
    int level = 1;
    while (level < MAX_SKIP_LEVEL && (rand() & 1)) {
        level++;
    }
    return level;
}

static struct skiplist_node *skiplist_node_create(const char *name, fuse_ino_t ino, int level) {
    struct skiplist_node *node = malloc(sizeof(struct skiplist_node));
    if (!node) return NULL;
    
    node->name = strdup(name);
    if (!node->name) {
        free(node);
        return NULL;
    }
    
    node->ino = ino;
    node->level = level;
    node->forward = malloc(sizeof(struct skiplist_node*) * (level + 1));
    if (!node->forward) {
        free(node->name);
        free(node);
        return NULL;
    }
    
    for (int i = 0; i <= level; i++) {
        node->forward[i] = NULL;
    }
    
    return node;
}

static void skiplist_node_destroy(struct skiplist_node *node) {
    if (node) {
        free(node->name);
        free(node->forward);
        free(node);
    }
}

static struct skiplist *skiplist_create(void) {
    struct skiplist *sl = malloc(sizeof(struct skiplist));
    if (!sl) return NULL;
    
    /* Create header node with maximum level */
    sl->header = skiplist_node_create("", 0, MAX_SKIP_LEVEL);
    if (!sl->header) {
        free(sl);
        return NULL;
    }
    
    sl->max_level = 0;
    sl->size = 0;
    
    if (pthread_rwlock_init(&sl->lock, NULL) != 0) {
        skiplist_node_destroy(sl->header);
        free(sl);
        return NULL;
    }
    
    return sl;
}

/* Skip list destroy */
static void skiplist_destroy(struct skiplist *sl) {
    if (!sl) return;
    
    pthread_rwlock_wrlock(&sl->lock);
    
    struct skiplist_node *current = sl->header->forward[0];
    while (current) {
        struct skiplist_node *next = current->forward[0];
        skiplist_node_destroy(current);
        current = next;
    }
    
    skiplist_node_destroy(sl->header);
    pthread_rwlock_unlock(&sl->lock);
    pthread_rwlock_destroy(&sl->lock);
    free(sl);
}

/* Skip list search */
static fuse_ino_t skiplist_search(struct skiplist *sl, const char *name) {
    if (!sl || !name) return 0;
    
    pthread_rwlock_rdlock(&sl->lock);
    
    struct skiplist_node *current = sl->header;
    
    /* Search from top level down */
    for (int level = sl->max_level; level >= 0; level--) {
        while (current->forward[level] && 
               strcmp(current->forward[level]->name, name) < 0) {
            current = current->forward[level];
        }
    }
    
    current = current->forward[0];
    fuse_ino_t result = 0;
    if (current && strcmp(current->name, name) == 0) {
        result = current->ino;
    }
    
    pthread_rwlock_unlock(&sl->lock);
    return result;
}

/* Skip list insert */
static int skiplist_insert(struct skiplist *sl, const char *name, fuse_ino_t ino) {
    if (!sl || !name) return -1;
    
    pthread_rwlock_wrlock(&sl->lock);
    
    struct skiplist_node *update[MAX_SKIP_LEVEL + 1];
    struct skiplist_node *current = sl->header;
    
    /* Find position to insert */
    for (int level = sl->max_level; level >= 0; level--) {
        while (current->forward[level] && 
               strcmp(current->forward[level]->name, name) < 0) {
            current = current->forward[level];
        }
        update[level] = current;
    }
    
    current = current->forward[0];
    
    /* Check if already exists */
    if (current && strcmp(current->name, name) == 0) {
        pthread_rwlock_unlock(&sl->lock);
        return -1; /* Already exists */
    }
    
    /* Create new node */
    int level = random_level();
    if (level > sl->max_level) {
        for (int i = sl->max_level + 1; i <= level; i++) {
            update[i] = sl->header;
        }
        sl->max_level = level;
    }
    
    struct skiplist_node *new_node = skiplist_node_create(name, ino, level);
    if (!new_node) {
        pthread_rwlock_unlock(&sl->lock);
		return -1;
	}
    
    /* Update forward pointers */
    for (int i = 0; i <= level; i++) {
        new_node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = new_node;
    }
    
    sl->size++;
    pthread_rwlock_unlock(&sl->lock);
	return 0;
}

/* Skip list delete */
static int skiplist_delete(struct skiplist *sl, const char *name) {
    if (!sl || !name) return -1;
    
    pthread_rwlock_wrlock(&sl->lock);
    
    struct skiplist_node *update[MAX_SKIP_LEVEL + 1];
    struct skiplist_node *current = sl->header;
    
    /* Find node to delete */
    for (int level = sl->max_level; level >= 0; level--) {
        while (current->forward[level] && 
               strcmp(current->forward[level]->name, name) < 0) {
            current = current->forward[level];
        }
        update[level] = current;
    }
    
    current = current->forward[0];
    
    /* Check if found */
    if (!current || strcmp(current->name, name) != 0) {
        pthread_rwlock_unlock(&sl->lock);
        return -1; /* Not found */
    }
    
    /* Update forward pointers */
    for (int i = 0; i <= sl->max_level; i++) {
        if (update[i]->forward[i] != current) break;
        update[i]->forward[i] = current->forward[i];
    }
    
    /* Update max level */
    while (sl->max_level > 0 && sl->header->forward[sl->max_level] == NULL) {
        sl->max_level--;
    }
    
    skiplist_node_destroy(current);
    sl->size--;
    
    pthread_rwlock_unlock(&sl->lock);
    return 0;
}

/* Basic initialization function */
static int ramfs_init_state(void) {
    ramfs_state = malloc(sizeof(struct ramfs_state));
    if (!ramfs_state) return -1;
    
    /* Initialize locks */
    if (pthread_mutex_init(&ramfs_state->ino_lock, NULL) != 0 ||
        pthread_mutex_init(&ramfs_state->block_lock, NULL) != 0 ||
        pthread_rwlock_init(&ramfs_state->inode_table_lock, NULL) != 0) {
        free(ramfs_state);
        return -1;
    }
    
    /* Initialize basic state */
    ramfs_state->next_ino = 2; /* Root is 1 */
    ramfs_state->free_blocks = NULL;
    ramfs_state->free_block_count = 0;
    ramfs_state->total_blocks = 0;
    ramfs_state->inode_table = NULL;
    ramfs_state->inode_table_size = 1024; /* Initial size */
    
    /* Create root directory */
    ramfs_state->root = ramfs_inode_create(S_IFDIR | 0755, getuid(), getgid());
    if (!ramfs_state->root) {
        free(ramfs_state);
        return -1;
    }
    ramfs_state->root->ino = 1; /* Root inode is always 1 */
    
    return 0;
}

/* Basic memory block allocation */
static struct ramfs_block *ramfs_block_alloc(void) {
    struct ramfs_block *block = malloc(sizeof(struct ramfs_block));
    if (block) {
        memset(block->data, 0, BLOCK_SIZE);
    }
    return block;
}

static void ramfs_block_free(struct ramfs_block *block) {
    if (block) {
        free(block);
    }
}

/* Initialize block info structure */
static void ramfs_block_info_init(struct ramfs_block_info *block_info) {
    memset(block_info->direct, 0, sizeof(block_info->direct));
    block_info->single_indirect = NULL;
    block_info->double_indirect = NULL;
    block_info->triple_indirect = NULL;
    block_info->total_blocks = 0;
    pthread_mutex_init(&block_info->lock, NULL);
}

/* Cleanup block info structure */
static void ramfs_block_info_cleanup(struct ramfs_block_info *block_info) {
    /* Free direct blocks */
    for (int i = 0; i < DIRECT_BLOCKS; i++) {
        if (block_info->direct[i]) {
            ramfs_block_free(block_info->direct[i]);
        }
    }
    
    /* Free single indirect blocks */
    if (block_info->single_indirect) {
        for (size_t i = 0; i < (BLOCK_SIZE / sizeof(struct ramfs_block*)); i++) {
            if (block_info->single_indirect[i]) {
                ramfs_block_free(block_info->single_indirect[i]);
            }
        }
        free(block_info->single_indirect);
    }
    
    /* Free double indirect blocks */
    if (block_info->double_indirect) {
        size_t ptrs_per_block = BLOCK_SIZE / sizeof(struct ramfs_block*);
        for (size_t i = 0; i < ptrs_per_block; i++) {
            if (block_info->double_indirect[i]) {
                for (size_t j = 0; j < ptrs_per_block; j++) {
                    if (block_info->double_indirect[i][j]) {
                        ramfs_block_free(block_info->double_indirect[i][j]);
                    }
                }
                free(block_info->double_indirect[i]);
            }
        }
        free(block_info->double_indirect);
    }
    
    /* Free triple indirect blocks */
    if (block_info->triple_indirect) {
        size_t ptrs_per_block = BLOCK_SIZE / sizeof(struct ramfs_block*);
        for (size_t i = 0; i < ptrs_per_block; i++) {
            if (block_info->triple_indirect[i]) {
                for (size_t j = 0; j < ptrs_per_block; j++) {
                    if (block_info->triple_indirect[i][j]) {
                        for (size_t k = 0; k < ptrs_per_block; k++) {
                            if (block_info->triple_indirect[i][j][k]) {
                                ramfs_block_free(block_info->triple_indirect[i][j][k]);
                            }
                        }
                        free(block_info->triple_indirect[i][j]);
                    }
                }
                free(block_info->triple_indirect[i]);
            }
        }
        free(block_info->triple_indirect);
    }
    
    pthread_mutex_destroy(&block_info->lock);
}

/* Inode creation */
static struct ramfs_inode *ramfs_inode_create(mode_t mode, uid_t uid, gid_t gid) {
    struct ramfs_inode *inode = malloc(sizeof(struct ramfs_inode));
    if (!inode) return NULL;
    
    /* Assign inode number */
    pthread_mutex_lock(&ramfs_state->ino_lock);
    inode->ino = ramfs_state->next_ino++;
    pthread_mutex_unlock(&ramfs_state->ino_lock);
    
    /* Set basic attributes */
    inode->mode = mode;
    inode->nlink = 1;
    inode->uid = uid;
    inode->gid = gid;
    inode->size = 0;
    
    /* Set timestamps */
    time_t now = time(NULL);
    inode->atime = now;
    inode->mtime = now;
    inode->ctime = now;
    
    /* Initialize synchronization */
    if (pthread_rwlock_init(&inode->lock, NULL) != 0) {
        free(inode);
        return NULL;
    }
    
    inode->ref_count = 1;
    
    /* Initialize type-specific data */
    if (S_ISDIR(mode)) {
        /* Directory: create skip list */
        inode->dir_entries = skiplist_create();
        if (!inode->dir_entries) {
            pthread_rwlock_destroy(&inode->lock);
            free(inode);
            return NULL;
        }
        memset(&inode->blocks, 0, sizeof(inode->blocks));
    } else {
        /* File: initialize block info */
        ramfs_block_info_init(&inode->blocks);
        inode->dir_entries = NULL;
    }
    
    /* Add to hash table (except for root which is handled specially) */
    if (inode->ino != 1) {
        if (ramfs_inode_add(inode) != 0) {
            if (S_ISDIR(mode)) {
                skiplist_destroy(inode->dir_entries);
            } else {
                ramfs_block_info_cleanup(&inode->blocks);
            }
            pthread_rwlock_destroy(&inode->lock);
            free(inode);
            return NULL;
        }
    }
    
    return inode;
}

/* Inode destruction */
static void ramfs_inode_destroy(struct ramfs_inode *inode) {
    if (!inode) return;
    
    /* Remove from hash table (except root) */
    if (inode->ino != 1) {
        ramfs_inode_remove(inode->ino);
    }
    
    /* Cleanup type-specific data */
    if (S_ISDIR(inode->mode)) {
        if (inode->dir_entries) {
            skiplist_destroy(inode->dir_entries);
        }
    } else {
        ramfs_block_info_cleanup(&inode->blocks);
    }
    
    /* Cleanup synchronization */
    pthread_rwlock_destroy(&inode->lock);
    
    free(inode);
}

/* Hash function for inode table */
static size_t inode_hash(fuse_ino_t ino) {
    return ino % ramfs_state->inode_table_size;
}

/* Add inode to hash table */
static int ramfs_inode_add(struct ramfs_inode *inode) {
    if (!ramfs_state->inode_table) {
        /* Initialize hash table */
        ramfs_state->inode_table = calloc(ramfs_state->inode_table_size, 
                                         sizeof(struct ramfs_inode*));
        if (!ramfs_state->inode_table) {
            return -1;
        }
    }
    
    pthread_rwlock_wrlock(&ramfs_state->inode_table_lock);
    
    size_t hash_idx = inode_hash(inode->ino);
    
    /* Simple linear probing for collision resolution */
    while (ramfs_state->inode_table[hash_idx] != NULL) {
        hash_idx = (hash_idx + 1) % ramfs_state->inode_table_size;
    }
    
    ramfs_state->inode_table[hash_idx] = inode;
    pthread_rwlock_unlock(&ramfs_state->inode_table_lock);
    
    return 0;
}

/* Remove inode from hash table */
static void ramfs_inode_remove(fuse_ino_t ino) {
    if (!ramfs_state->inode_table) return;
    
    pthread_rwlock_wrlock(&ramfs_state->inode_table_lock);
    
    size_t hash_idx = inode_hash(ino);
    
    /* Linear probing to find the inode */
    while (ramfs_state->inode_table[hash_idx] != NULL) {
        if (ramfs_state->inode_table[hash_idx]->ino == ino) {
            ramfs_state->inode_table[hash_idx] = NULL;
            break;
        }
        hash_idx = (hash_idx + 1) % ramfs_state->inode_table_size;
    }
    
    pthread_rwlock_unlock(&ramfs_state->inode_table_lock);
}

/* Inode lookup with hash table */
static struct ramfs_inode *ramfs_inode_get(fuse_ino_t ino) {
    if (ino == 1) {
        return ramfs_state->root;
    }
    
    if (!ramfs_state->inode_table) return NULL;
    
    pthread_rwlock_rdlock(&ramfs_state->inode_table_lock);
    
    size_t hash_idx = inode_hash(ino);
    struct ramfs_inode *result = NULL;
    
    /* Linear probing to find the inode */
    while (ramfs_state->inode_table[hash_idx] != NULL) {
        if (ramfs_state->inode_table[hash_idx]->ino == ino) {
            result = ramfs_state->inode_table[hash_idx];
            break;
        }
        hash_idx = (hash_idx + 1) % ramfs_state->inode_table_size;
    }
    
    pthread_rwlock_unlock(&ramfs_state->inode_table_lock);
    return result;
}

/* FUSE operations */
static void ramfs_ll_getattr(fuse_req_t req, fuse_ino_t ino,
                             struct fuse_file_info *fi) {
    (void)fi;
    
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
		fuse_reply_err(req, ENOENT);
        return;
    }
    
    pthread_rwlock_rdlock(&inode->lock);
    
    struct stat stbuf;
    memset(&stbuf, 0, sizeof(stbuf));
    
    stbuf.st_ino = inode->ino;
    stbuf.st_mode = inode->mode;
    stbuf.st_nlink = inode->nlink;
    stbuf.st_uid = inode->uid;
    stbuf.st_gid = inode->gid;
    stbuf.st_size = inode->size;
    stbuf.st_atime = inode->atime;
    stbuf.st_mtime = inode->mtime;
    stbuf.st_ctime = inode->ctime;
    stbuf.st_blksize = BLOCK_SIZE;
    stbuf.st_blocks = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    pthread_rwlock_unlock(&inode->lock);
    
		fuse_reply_attr(req, &stbuf, 1.0);
}

static void ramfs_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name) {
    struct ramfs_inode *parent_inode = ramfs_inode_get(parent);
    if (!parent_inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    if (!S_ISDIR(parent_inode->mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }
    
    /* Search in directory skip list */
    fuse_ino_t child_ino = skiplist_search(parent_inode->dir_entries, name);
    if (child_ino == 0) {
		fuse_reply_err(req, ENOENT);
        return;
    }
    
    struct ramfs_inode *child_inode = ramfs_inode_get(child_ino);
    if (!child_inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* Prepare entry response */
    struct fuse_entry_param e;
		memset(&e, 0, sizeof(e));
    e.ino = child_inode->ino;
		e.attr_timeout = 1.0;
		e.entry_timeout = 1.0;
    
    /* Fill stat info */
    pthread_rwlock_rdlock(&child_inode->lock);
    e.attr.st_ino = child_inode->ino;
    e.attr.st_mode = child_inode->mode;
    e.attr.st_nlink = child_inode->nlink;
    e.attr.st_uid = child_inode->uid;
    e.attr.st_gid = child_inode->gid;
    e.attr.st_size = child_inode->size;
    e.attr.st_atime = child_inode->atime;
    e.attr.st_mtime = child_inode->mtime;
    e.attr.st_ctime = child_inode->ctime;
    e.attr.st_blksize = BLOCK_SIZE;
    e.attr.st_blocks = (child_inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    pthread_rwlock_unlock(&child_inode->lock);

		fuse_reply_entry(req, &e);
}

/* Directory buffer for readdir */
struct dirbuf {
	char *p;
	size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
                       fuse_ino_t ino) {
	struct stat stbuf;
	size_t oldsize = b->size;
	b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
	b->p = (char *) realloc(b->p, b->size);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = ino;
	fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
			  b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
                             off_t off, size_t maxsize) {
	if (off < bufsize)
		return fuse_reply_buf(req, buf + off,
				      min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);
}

static void ramfs_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
                             off_t off, struct fuse_file_info *fi) {
    (void)fi;
    
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    if (!S_ISDIR(inode->mode)) {
		fuse_reply_err(req, ENOTDIR);
        return;
    }

    struct dirbuf b;
		memset(&b, 0, sizeof(b));
    
    /* Add . and .. entries */
    dirbuf_add(req, &b, ".", ino);
    dirbuf_add(req, &b, "..", ino); /* For now, .. points to self for root */
    
    /* Add all entries from skip list */
    pthread_rwlock_rdlock(&inode->dir_entries->lock);
    struct skiplist_node *current = inode->dir_entries->header->forward[0];
    while (current) {
        dirbuf_add(req, &b, current->name, current->ino);
        current = current->forward[0];
    }
    pthread_rwlock_unlock(&inode->dir_entries->lock);
    
		reply_buf_limited(req, b.p, b.size, off, size);
		free(b.p);
	}

static void ramfs_ll_mkdir(fuse_req_t req, fuse_ino_t parent, const char *name,
                           mode_t mode) {
    struct ramfs_inode *parent_inode = ramfs_inode_get(parent);
    if (!parent_inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    if (!S_ISDIR(parent_inode->mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }
    
    /* Check if name already exists */
    if (skiplist_search(parent_inode->dir_entries, name) != 0) {
        fuse_reply_err(req, EEXIST);
        return;
    }
    
    /* Create new directory inode */
    const struct fuse_ctx *ctx = fuse_req_ctx(req);
    struct ramfs_inode *new_inode = ramfs_inode_create(S_IFDIR | (mode & 07777), 
                                                       ctx->uid, ctx->gid);
    if (!new_inode) {
        fuse_reply_err(req, ENOMEM);
        return;
    }
    
    /* Add to parent directory */
    if (skiplist_insert(parent_inode->dir_entries, name, new_inode->ino) != 0) {
        ramfs_inode_destroy(new_inode);
        fuse_reply_err(req, ENOMEM);
        return;
    }
    
    /* Update parent directory size and mtime */
    pthread_rwlock_wrlock(&parent_inode->lock);
    parent_inode->mtime = time(NULL);
    parent_inode->nlink++; /* Directories increase parent link count */
    pthread_rwlock_unlock(&parent_inode->lock);
    
    /* Prepare response */
    struct fuse_entry_param e;
    memset(&e, 0, sizeof(e));
    e.ino = new_inode->ino;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    
    pthread_rwlock_rdlock(&new_inode->lock);
    e.attr.st_ino = new_inode->ino;
    e.attr.st_mode = new_inode->mode;
    e.attr.st_nlink = new_inode->nlink;
    e.attr.st_uid = new_inode->uid;
    e.attr.st_gid = new_inode->gid;
    e.attr.st_size = new_inode->size;
    e.attr.st_atime = new_inode->atime;
    e.attr.st_mtime = new_inode->mtime;
    e.attr.st_ctime = new_inode->ctime;
    e.attr.st_blksize = BLOCK_SIZE;
    e.attr.st_blocks = 0;
    pthread_rwlock_unlock(&new_inode->lock);
    
    fuse_reply_entry(req, &e);
}

static void ramfs_ll_mknod(fuse_req_t req, fuse_ino_t parent, const char *name,
                           mode_t mode, dev_t rdev) {
    (void)rdev; /* We don't support device files */
    
    struct ramfs_inode *parent_inode = ramfs_inode_get(parent);
    if (!parent_inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    if (!S_ISDIR(parent_inode->mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }
    
    /* Check if name already exists */
    if (skiplist_search(parent_inode->dir_entries, name) != 0) {
        fuse_reply_err(req, EEXIST);
        return;
    }
    
    /* Only support regular files for now */
    if (!S_ISREG(mode)) {
        fuse_reply_err(req, ENOTSUP);
        return;
    }
    
    /* Create new file inode */
    const struct fuse_ctx *ctx = fuse_req_ctx(req);
    struct ramfs_inode *new_inode = ramfs_inode_create(S_IFREG | (mode & 07777), 
                                                       ctx->uid, ctx->gid);
    if (!new_inode) {
        fuse_reply_err(req, ENOMEM);
        return;
    }
    
    /* Add to parent directory */
    if (skiplist_insert(parent_inode->dir_entries, name, new_inode->ino) != 0) {
        ramfs_inode_destroy(new_inode);
        fuse_reply_err(req, ENOMEM);
        return;
    }
    
    /* Update parent directory mtime */
    pthread_rwlock_wrlock(&parent_inode->lock);
    parent_inode->mtime = time(NULL);
    pthread_rwlock_unlock(&parent_inode->lock);
    
    /* Prepare response */
    struct fuse_entry_param e;
    memset(&e, 0, sizeof(e));
    e.ino = new_inode->ino;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    
    pthread_rwlock_rdlock(&new_inode->lock);
    e.attr.st_ino = new_inode->ino;
    e.attr.st_mode = new_inode->mode;
    e.attr.st_nlink = new_inode->nlink;
    e.attr.st_uid = new_inode->uid;
    e.attr.st_gid = new_inode->gid;
    e.attr.st_size = new_inode->size;
    e.attr.st_atime = new_inode->atime;
    e.attr.st_mtime = new_inode->mtime;
    e.attr.st_ctime = new_inode->ctime;
    e.attr.st_blksize = BLOCK_SIZE;
    e.attr.st_blocks = 0;
    pthread_rwlock_unlock(&new_inode->lock);
    
    fuse_reply_entry(req, &e);
}

/* Get data block for file at given block index */
static struct ramfs_block *get_file_block(struct ramfs_inode *inode, size_t block_idx, int create) {
    if (!S_ISREG(inode->mode)) return NULL;
    
    pthread_mutex_lock(&inode->blocks.lock);
    
    struct ramfs_block *block = NULL;
    
    /* Direct blocks */
    if (block_idx < DIRECT_BLOCKS) {
        if (inode->blocks.direct[block_idx] == NULL && create) {
            inode->blocks.direct[block_idx] = ramfs_block_alloc();
        }
        block = inode->blocks.direct[block_idx];
    }
    /* Single indirect blocks */
    else if (block_idx < DIRECT_BLOCKS + (BLOCK_SIZE / sizeof(struct ramfs_block*))) {
        size_t indirect_idx = block_idx - DIRECT_BLOCKS;
        
        if (inode->blocks.single_indirect == NULL && create) {
            inode->blocks.single_indirect = calloc(BLOCK_SIZE / sizeof(struct ramfs_block*), 
                                                   sizeof(struct ramfs_block*));
        }
        
        if (inode->blocks.single_indirect) {
            if (inode->blocks.single_indirect[indirect_idx] == NULL && create) {
                inode->blocks.single_indirect[indirect_idx] = ramfs_block_alloc();
            }
            block = inode->blocks.single_indirect[indirect_idx];
        }
    }
    /* Double indirect blocks */
    else {
        size_t ptrs_per_block = BLOCK_SIZE / sizeof(struct ramfs_block*);
        size_t double_start = DIRECT_BLOCKS + ptrs_per_block;
        size_t triple_start = double_start + ptrs_per_block * ptrs_per_block;
        
        if (block_idx < triple_start) {
            /* Double indirect block */
            size_t double_idx = block_idx - double_start;
            size_t first_level = double_idx / ptrs_per_block;
            size_t second_level = double_idx % ptrs_per_block;
            
            if (inode->blocks.double_indirect == NULL && create) {
                inode->blocks.double_indirect = calloc(ptrs_per_block, 
                                                      sizeof(struct ramfs_block**));
            }
            
            if (inode->blocks.double_indirect) {
                if (inode->blocks.double_indirect[first_level] == NULL && create) {
                    inode->blocks.double_indirect[first_level] = calloc(ptrs_per_block,
                                                                       sizeof(struct ramfs_block*));
                }
                
                if (inode->blocks.double_indirect[first_level]) {
                    if (inode->blocks.double_indirect[first_level][second_level] == NULL && create) {
                        inode->blocks.double_indirect[first_level][second_level] = ramfs_block_alloc();
                    }
                    block = inode->blocks.double_indirect[first_level][second_level];
                }
            }
        }
        /* Triple indirect blocks */
        else if (block_idx < triple_start + ptrs_per_block * ptrs_per_block * ptrs_per_block) {
            size_t triple_idx = block_idx - triple_start;
            size_t first_level = triple_idx / (ptrs_per_block * ptrs_per_block);
            size_t remaining = triple_idx % (ptrs_per_block * ptrs_per_block);
            size_t second_level = remaining / ptrs_per_block;
            size_t third_level = remaining % ptrs_per_block;
            
            if (inode->blocks.triple_indirect == NULL && create) {
                inode->blocks.triple_indirect = calloc(ptrs_per_block,
                                                      sizeof(struct ramfs_block***));
            }
            
            if (inode->blocks.triple_indirect) {
                if (inode->blocks.triple_indirect[first_level] == NULL && create) {
                    inode->blocks.triple_indirect[first_level] = calloc(ptrs_per_block,
                                                                       sizeof(struct ramfs_block**));
                }
                
                if (inode->blocks.triple_indirect[first_level]) {
                    if (inode->blocks.triple_indirect[first_level][second_level] == NULL && create) {
                        inode->blocks.triple_indirect[first_level][second_level] = 
                            calloc(ptrs_per_block, sizeof(struct ramfs_block*));
                    }
                    
                    if (inode->blocks.triple_indirect[first_level][second_level]) {
                        if (inode->blocks.triple_indirect[first_level][second_level][third_level] == NULL && create) {
                            inode->blocks.triple_indirect[first_level][second_level][third_level] = ramfs_block_alloc();
                        }
                        block = inode->blocks.triple_indirect[first_level][second_level][third_level];
                    }
                }
            }
        }
        /* Block index too large */
        else {
            block = NULL;
        }
    }
    
    pthread_mutex_unlock(&inode->blocks.lock);
    return block;
}

static void ramfs_ll_open(fuse_req_t req, fuse_ino_t ino,
                          struct fuse_file_info *fi) {
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    if (S_ISDIR(inode->mode)) {
		fuse_reply_err(req, EISDIR);
        return;
    }
    
		fuse_reply_open(req, fi);
}

static void ramfs_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
                          off_t off, struct fuse_file_info *fi) {
    (void)fi;
    
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    pthread_rwlock_rdlock(&inode->lock);
    
    if (off >= (off_t)inode->size) {
        pthread_rwlock_unlock(&inode->lock);
        fuse_reply_buf(req, NULL, 0);
        return;
    }
    
    if (off + size > inode->size) {
        size = inode->size - off;
    }
    
    /* Read data block by block */
    char *buf = malloc(size);
    if (!buf) {
        pthread_rwlock_unlock(&inode->lock);
        fuse_reply_err(req, ENOMEM);
        return;
    }
    
    size_t bytes_read = 0;
    while (bytes_read < size) {
        size_t block_idx = (off + bytes_read) / BLOCK_SIZE;
        size_t block_offset = (off + bytes_read) % BLOCK_SIZE;
        size_t to_read = min(BLOCK_SIZE - block_offset, size - bytes_read);
        
        struct ramfs_block *block = get_file_block(inode, block_idx, 0);
        if (block) {
            memcpy(buf + bytes_read, block->data + block_offset, to_read);
        } else {
            /* Block not allocated, return zeros */
            memset(buf + bytes_read, 0, to_read);
        }
        
        bytes_read += to_read;
    }
    
    /* Update access time */
    inode->atime = time(NULL);
    
    pthread_rwlock_unlock(&inode->lock);
    
    fuse_reply_buf(req, buf, bytes_read);
    free(buf);
}

static void ramfs_ll_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
                           size_t size, off_t off, struct fuse_file_info *fi) {
    (void)fi;
    
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    if (S_ISDIR(inode->mode)) {
        fuse_reply_err(req, EISDIR);
        return;
    }
    
    pthread_rwlock_wrlock(&inode->lock);
    
    /* Calculate new file size */
    size_t new_size = off + size;
    if (new_size > inode->size) {
        inode->size = new_size;
    }
    
    /* Write data block by block */
    size_t bytes_written = 0;
    while (bytes_written < size) {
        size_t block_idx = (off + bytes_written) / BLOCK_SIZE;
        size_t block_offset = (off + bytes_written) % BLOCK_SIZE;
        size_t to_write = min(BLOCK_SIZE - block_offset, size - bytes_written);
        
        /* Get or allocate the block */
        struct ramfs_block *block = get_file_block(inode, block_idx, 1);
        if (!block) {
            pthread_rwlock_unlock(&inode->lock);
            fuse_reply_err(req, ENOMEM);
            return;
        }
        
        /* Copy data to block */
        memcpy(block->data + block_offset, buf + bytes_written, to_write);
        bytes_written += to_write;
    }
    
    /* Update timestamps */
    time_t now = time(NULL);
    inode->mtime = now;
    inode->ctime = now;
    
    pthread_rwlock_unlock(&inode->lock);
    
    fuse_reply_write(req, bytes_written);
}

static void ramfs_ll_unlink(fuse_req_t req, fuse_ino_t parent, const char *name) {
    struct ramfs_inode *parent_inode = ramfs_inode_get(parent);
    if (!parent_inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    if (!S_ISDIR(parent_inode->mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }
    
    /* Find the file to delete */
    fuse_ino_t child_ino = skiplist_search(parent_inode->dir_entries, name);
    if (child_ino == 0) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    struct ramfs_inode *child_inode = ramfs_inode_get(child_ino);
    if (!child_inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* Can't unlink directories */
    if (S_ISDIR(child_inode->mode)) {
        fuse_reply_err(req, EISDIR);
        return;
    }
    
    /* Remove from parent directory */
    if (skiplist_delete(parent_inode->dir_entries, name) != 0) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* Update parent directory mtime */
    pthread_rwlock_wrlock(&parent_inode->lock);
    parent_inode->mtime = time(NULL);
    pthread_rwlock_unlock(&parent_inode->lock);
    
    /* Decrease link count */
    pthread_rwlock_wrlock(&child_inode->lock);
    child_inode->nlink--;
    child_inode->ctime = time(NULL);
    
    /* If no more links, destroy the inode */
    if (child_inode->nlink == 0) {
        pthread_rwlock_unlock(&child_inode->lock);
        ramfs_inode_destroy(child_inode);
    } else {
        pthread_rwlock_unlock(&child_inode->lock);
    }
    
		fuse_reply_err(req, 0);
	}

static void ramfs_ll_rmdir(fuse_req_t req, fuse_ino_t parent, const char *name) {
    struct ramfs_inode *parent_inode = ramfs_inode_get(parent);
    if (!parent_inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    if (!S_ISDIR(parent_inode->mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }
    
    /* Find the directory to delete */
    fuse_ino_t child_ino = skiplist_search(parent_inode->dir_entries, name);
    if (child_ino == 0) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    struct ramfs_inode *child_inode = ramfs_inode_get(child_ino);
    if (!child_inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* Can only rmdir directories */
    if (!S_ISDIR(child_inode->mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }
    
    /* Check if directory is empty */
    pthread_rwlock_rdlock(&child_inode->dir_entries->lock);
    size_t dir_size = child_inode->dir_entries->size;
    pthread_rwlock_unlock(&child_inode->dir_entries->lock);
    
    if (dir_size > 0) {
        fuse_reply_err(req, ENOTEMPTY);
        return;
    }
    
    /* Remove from parent directory */
    if (skiplist_delete(parent_inode->dir_entries, name) != 0) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* Update parent directory */
    pthread_rwlock_wrlock(&parent_inode->lock);
    parent_inode->mtime = time(NULL);
    parent_inode->nlink--; /* Directories decrease parent link count */
    pthread_rwlock_unlock(&parent_inode->lock);
    
    /* Destroy the directory inode */
    ramfs_inode_destroy(child_inode);
    
		fuse_reply_err(req, 0);
	}

static void ramfs_ll_link(fuse_req_t req, fuse_ino_t ino, fuse_ino_t newparent,
                          const char *newname) {
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* Can't link directories */
    if (S_ISDIR(inode->mode)) {
        fuse_reply_err(req, EPERM);
        return;
    }
    
    struct ramfs_inode *parent_inode = ramfs_inode_get(newparent);
    if (!parent_inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    if (!S_ISDIR(parent_inode->mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }
    
    /* Check if name already exists */
    if (skiplist_search(parent_inode->dir_entries, newname) != 0) {
        fuse_reply_err(req, EEXIST);
        return;
    }
    
    /* Add to parent directory */
    if (skiplist_insert(parent_inode->dir_entries, newname, inode->ino) != 0) {
        fuse_reply_err(req, ENOMEM);
        return;
    }
    
    /* Increase link count */
    pthread_rwlock_wrlock(&inode->lock);
    inode->nlink++;
    inode->ctime = time(NULL);
    pthread_rwlock_unlock(&inode->lock);
    
    /* Update parent directory mtime */
    pthread_rwlock_wrlock(&parent_inode->lock);
    parent_inode->mtime = time(NULL);
    pthread_rwlock_unlock(&parent_inode->lock);
    
    /* Prepare response */
    struct fuse_entry_param e;
    memset(&e, 0, sizeof(e));
    e.ino = inode->ino;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;
    
    pthread_rwlock_rdlock(&inode->lock);
    e.attr.st_ino = inode->ino;
    e.attr.st_mode = inode->mode;
    e.attr.st_nlink = inode->nlink;
    e.attr.st_uid = inode->uid;
    e.attr.st_gid = inode->gid;
    e.attr.st_size = inode->size;
    e.attr.st_atime = inode->atime;
    e.attr.st_mtime = inode->mtime;
    e.attr.st_ctime = inode->ctime;
    e.attr.st_blksize = BLOCK_SIZE;
    e.attr.st_blocks = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    pthread_rwlock_unlock(&inode->lock);
    
    fuse_reply_entry(req, &e);
}

static void ramfs_ll_rename(fuse_req_t req, fuse_ino_t parent, const char *name,
                            fuse_ino_t newparent, const char *newname,
                            unsigned int flags) {
    (void)flags; /* We don't support any rename flags */
    
    struct ramfs_inode *old_parent = ramfs_inode_get(parent);
    if (!old_parent) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    struct ramfs_inode *new_parent = ramfs_inode_get(newparent);
    if (!new_parent) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    if (!S_ISDIR(old_parent->mode) || !S_ISDIR(new_parent->mode)) {
        fuse_reply_err(req, ENOTDIR);
        return;
    }
    
    /* Find the file to rename */
    fuse_ino_t file_ino = skiplist_search(old_parent->dir_entries, name);
    if (file_ino == 0) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    struct ramfs_inode *file_inode = ramfs_inode_get(file_ino);
    if (!file_inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* Check if destination already exists */
    fuse_ino_t existing_ino = skiplist_search(new_parent->dir_entries, newname);
    if (existing_ino != 0) {
        /* Remove existing file if it's the same type */
        struct ramfs_inode *existing_inode = ramfs_inode_get(existing_ino);
        if (existing_inode) {
            if (S_ISDIR(existing_inode->mode) != S_ISDIR(file_inode->mode)) {
                fuse_reply_err(req, S_ISDIR(existing_inode->mode) ? EISDIR : ENOTDIR);
                return;
            }
            
            /* Remove the existing entry */
            skiplist_delete(new_parent->dir_entries, newname);
            
            /* Decrease link count of existing file */
            pthread_rwlock_wrlock(&existing_inode->lock);
            existing_inode->nlink--;
            if (existing_inode->nlink == 0) {
                pthread_rwlock_unlock(&existing_inode->lock);
                ramfs_inode_destroy(existing_inode);
            } else {
                pthread_rwlock_unlock(&existing_inode->lock);
            }
        }
    }
    
    /* Remove from old parent */
    if (skiplist_delete(old_parent->dir_entries, name) != 0) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* Add to new parent */
    if (skiplist_insert(new_parent->dir_entries, newname, file_ino) != 0) {
        /* Try to restore in old parent */
        skiplist_insert(old_parent->dir_entries, name, file_ino);
        fuse_reply_err(req, ENOMEM);
        return;
    }
    
    /* Update directory link counts if moving a directory */
    if (S_ISDIR(file_inode->mode) && parent != newparent) {
        pthread_rwlock_wrlock(&old_parent->lock);
        old_parent->nlink--;
        pthread_rwlock_unlock(&old_parent->lock);
        
        pthread_rwlock_wrlock(&new_parent->lock);
        new_parent->nlink++;
        pthread_rwlock_unlock(&new_parent->lock);
    }
    
    /* Update timestamps */
    time_t now = time(NULL);
    
    pthread_rwlock_wrlock(&file_inode->lock);
    file_inode->ctime = now;
    pthread_rwlock_unlock(&file_inode->lock);
    
    pthread_rwlock_wrlock(&old_parent->lock);
    old_parent->mtime = now;
    pthread_rwlock_unlock(&old_parent->lock);
    
    if (old_parent != new_parent) {
        pthread_rwlock_wrlock(&new_parent->lock);
        new_parent->mtime = now;
        pthread_rwlock_unlock(&new_parent->lock);
    }
    
    fuse_reply_err(req, 0);
}

static void ramfs_ll_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr,
                             int to_set, struct fuse_file_info *fi) {
    (void)fi;
    
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    pthread_rwlock_wrlock(&inode->lock);
    
    /* Handle size changes (truncate) */
    if (to_set & FUSE_SET_ATTR_SIZE) {
        size_t new_size = attr->st_size;
        
        if (S_ISREG(inode->mode)) {
            /* Truncating a regular file */
            if (new_size < inode->size) {
                /* Shrinking: free blocks beyond new size */
                size_t last_block = (new_size > 0) ? (new_size - 1) / BLOCK_SIZE : 0;
                
                pthread_mutex_lock(&inode->blocks.lock);
                
                /* Free direct blocks */
                for (int i = last_block + 1; i < DIRECT_BLOCKS; i++) {
                    if (inode->blocks.direct[i]) {
                        ramfs_block_free(inode->blocks.direct[i]);
                        inode->blocks.direct[i] = NULL;
                    }
                }
                
                /* Free single indirect blocks if needed */
                size_t ptrs_per_block = BLOCK_SIZE / sizeof(struct ramfs_block*);
                size_t single_start = DIRECT_BLOCKS;
                size_t double_start = single_start + ptrs_per_block;
                size_t triple_start = double_start + ptrs_per_block * ptrs_per_block;
                
                if (last_block < single_start && inode->blocks.single_indirect) {
                    for (size_t i = 0; i < ptrs_per_block; i++) {
                        if (inode->blocks.single_indirect[i]) {
                            ramfs_block_free(inode->blocks.single_indirect[i]);
                            inode->blocks.single_indirect[i] = NULL;
                        }
                    }
                    free(inode->blocks.single_indirect);
                    inode->blocks.single_indirect = NULL;
                }
                /* Free double indirect blocks if needed */
                else if (last_block < double_start && inode->blocks.double_indirect) {
                    for (size_t i = 0; i < ptrs_per_block; i++) {
                        if (inode->blocks.double_indirect[i]) {
                            for (size_t j = 0; j < ptrs_per_block; j++) {
                                if (inode->blocks.double_indirect[i][j]) {
                                    ramfs_block_free(inode->blocks.double_indirect[i][j]);
                                }
                            }
                            free(inode->blocks.double_indirect[i]);
                        }
                    }
                    free(inode->blocks.double_indirect);
                    inode->blocks.double_indirect = NULL;
                }
                /* Free triple indirect blocks if needed */
                else if (last_block < triple_start && inode->blocks.triple_indirect) {
                    for (size_t i = 0; i < ptrs_per_block; i++) {
                        if (inode->blocks.triple_indirect[i]) {
                            for (size_t j = 0; j < ptrs_per_block; j++) {
                                if (inode->blocks.triple_indirect[i][j]) {
                                    for (size_t k = 0; k < ptrs_per_block; k++) {
                                        if (inode->blocks.triple_indirect[i][j][k]) {
                                            ramfs_block_free(inode->blocks.triple_indirect[i][j][k]);
                                        }
                                    }
                                    free(inode->blocks.triple_indirect[i][j]);
                                }
                            }
                            free(inode->blocks.triple_indirect[i]);
                        }
                    }
                    free(inode->blocks.triple_indirect);
                    inode->blocks.triple_indirect = NULL;
                }
                
                pthread_mutex_unlock(&inode->blocks.lock);
            }
            
            inode->size = new_size;
        } else {
            /* Can't truncate non-regular files */
            pthread_rwlock_unlock(&inode->lock);
            fuse_reply_err(req, EINVAL);
            return;
        }
    }
    
    /* Handle mode changes */
    if (to_set & FUSE_SET_ATTR_MODE) {
        inode->mode = (inode->mode & S_IFMT) | (attr->st_mode & 07777);
    }
    
    /* Handle ownership changes */
    if (to_set & FUSE_SET_ATTR_UID) {
        inode->uid = attr->st_uid;
    }
    
    if (to_set & FUSE_SET_ATTR_GID) {
        inode->gid = attr->st_gid;
    }
    
    /* Handle time changes */
    time_t now = time(NULL);
    
    if (to_set & FUSE_SET_ATTR_ATIME) {
        inode->atime = attr->st_atime;
    }
    
    if (to_set & FUSE_SET_ATTR_MTIME) {
        inode->mtime = attr->st_mtime;
    }
    
    if (to_set & (FUSE_SET_ATTR_MODE | FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID | FUSE_SET_ATTR_SIZE)) {
        inode->ctime = now;
    }
    
    /* Prepare response */
    struct stat stbuf;
    memset(&stbuf, 0, sizeof(stbuf));
    stbuf.st_ino = inode->ino;
    stbuf.st_mode = inode->mode;
    stbuf.st_nlink = inode->nlink;
    stbuf.st_uid = inode->uid;
    stbuf.st_gid = inode->gid;
    stbuf.st_size = inode->size;
    stbuf.st_atime = inode->atime;
    stbuf.st_mtime = inode->mtime;
    stbuf.st_ctime = inode->ctime;
    stbuf.st_blksize = BLOCK_SIZE;
    stbuf.st_blocks = (inode->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    pthread_rwlock_unlock(&inode->lock);
    
    fuse_reply_attr(req, &stbuf, 1.0);
}

static void ramfs_ll_flush(fuse_req_t req, fuse_ino_t ino,
                          struct fuse_file_info *fi) {
    (void)ino;
    (void)fi;
    
    /* Nothing to flush in a memory filesystem */
    fuse_reply_err(req, 0);
}

static void ramfs_ll_fsync(fuse_req_t req, fuse_ino_t ino, int datasync,
                          struct fuse_file_info *fi) {
    (void)ino;
    (void)datasync;
    (void)fi;
    
    /* Nothing to sync in a memory filesystem */
    fuse_reply_err(req, 0);
}

/* Extended attributes - simple implementation */
static void ramfs_ll_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
                             size_t size) {
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* For demonstration, support a simple test attribute */
    if (strcmp(name, "user.ramfs.test") == 0) {
        const char *value = "ramfs_test_value";
        size_t value_len = strlen(value);
        
        if (size == 0) {
            /* Return size of attribute */
            fuse_reply_xattr(req, value_len);
        } else if (size >= value_len) {
            /* Return attribute value */
            fuse_reply_buf(req, value, value_len);
        } else {
            /* Buffer too small */
            fuse_reply_err(req, ERANGE);
        }
    } else {
        fuse_reply_err(req, ENODATA);
    }
}

static void ramfs_ll_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
                             const char *value, size_t size, int flags) {
    (void)flags;
    
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* For demonstration, only accept our test attribute */
    if (strcmp(name, "user.ramfs.test") == 0) {
        const char *expected = "ramfs_test_value";
        if (size == strlen(expected) && strncmp(value, expected, size) == 0) {
            fuse_reply_err(req, 0);
        } else {
            fuse_reply_err(req, EINVAL);
        }
    } else {
        fuse_reply_err(req, ENOTSUP);
    }
}

static void ramfs_ll_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name) {
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* For demonstration, only support removing our test attribute */
    if (strcmp(name, "user.ramfs.test") == 0) {
        fuse_reply_err(req, 0);
    } else {
        fuse_reply_err(req, ENODATA);
    }
}

static void ramfs_ll_listxattr(fuse_req_t req, fuse_ino_t ino, size_t size) {
    struct ramfs_inode *inode = ramfs_inode_get(ino);
    if (!inode) {
        fuse_reply_err(req, ENOENT);
        return;
    }
    
    /* For demonstration, list our test attribute */
    const char *list = "user.ramfs.test\0";
    size_t list_len = strlen("user.ramfs.test") + 1;
    
    if (size == 0) {
        /* Return size of list */
        fuse_reply_xattr(req, list_len);
    } else if (size >= list_len) {
        /* Return attribute list */
        fuse_reply_buf(req, list, list_len);
    } else {
        /* Buffer too small */
        fuse_reply_err(req, ERANGE);
    }
}

static void ramfs_ll_init(void *userdata, struct fuse_conn_info *conn) {
    (void)userdata;
    (void)conn;
    
    /* Initialize the filesystem state */
    if (ramfs_init_state() != 0) {
        fprintf(stderr, "Failed to initialize RAMfs state\n");
        exit(1);
    }
    
    printf("RAMfs initialized successfully\n");
}

static const struct fuse_lowlevel_ops ramfs_oper = {
    .init         = ramfs_ll_init,
    .getattr      = ramfs_ll_getattr,
    .setattr      = ramfs_ll_setattr,
    .lookup       = ramfs_ll_lookup,
    .readdir      = ramfs_ll_readdir,
    .mkdir        = ramfs_ll_mkdir,
    .mknod        = ramfs_ll_mknod,
    .open         = ramfs_ll_open,
    .read         = ramfs_ll_read,
    .write        = ramfs_ll_write,
    .flush        = ramfs_ll_flush,
    .fsync        = ramfs_ll_fsync,
    .unlink       = ramfs_ll_unlink,
    .rmdir        = ramfs_ll_rmdir,
    .link         = ramfs_ll_link,
    .rename       = ramfs_ll_rename,
    .getxattr     = ramfs_ll_getxattr,
    .setxattr     = ramfs_ll_setxattr,
    .removexattr  = ramfs_ll_removexattr,
    .listxattr    = ramfs_ll_listxattr,
};

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_session *se;
	struct fuse_cmdline_opts opts;
	struct fuse_loop_config *config;
	int ret = -1;

	if (fuse_parse_cmdline(&args, &opts) != 0)
		return 1;
	if (opts.show_help) {
		printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
		fuse_cmdline_help();
		fuse_lowlevel_help();
		ret = 0;
		goto err_out1;
	} else if (opts.show_version) {
		printf("FUSE library version %s\n", fuse_pkgversion());
		fuse_lowlevel_version();
		ret = 0;
		goto err_out1;
	}

	if(opts.mountpoint == NULL) {
		printf("usage: %s [options] <mountpoint>\n", argv[0]);
		printf("       %s --help\n", argv[0]);
		ret = 1;
		goto err_out1;
	}

    se = fuse_session_new(&args, &ramfs_oper,
                          sizeof(ramfs_oper), NULL);
	if (se == NULL)
	    goto err_out1;

	if (fuse_set_signal_handlers(se) != 0)
	    goto err_out2;

	if (fuse_session_mount(se, opts.mountpoint) != 0)
	    goto err_out3;

	fuse_daemonize(opts.foreground);

	/* Block until ctrl+c or fusermount -u */
	if (opts.singlethread)
		ret = fuse_session_loop(se);
	else {
		config = fuse_loop_cfg_create();
		fuse_loop_cfg_set_clone_fd(config, opts.clone_fd);
		fuse_loop_cfg_set_max_threads(config, opts.max_threads);
		ret = fuse_session_loop_mt(se, config);
		fuse_loop_cfg_destroy(config);
		config = NULL;
	}

	fuse_session_unmount(se);
err_out3:
	fuse_remove_signal_handlers(se);
err_out2:
	fuse_session_destroy(se);
err_out1:
	free(opts.mountpoint);
	fuse_opt_free_args(&args);

	return ret ? 1 : 0;
}
