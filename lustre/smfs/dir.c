/*
 * dir.c
 */
#define DEBUG_SUBSYSTEM S_SNAP

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/unistd.h>

#include "smfs_internal.h" 

static void d_unalloc(struct dentry *dentry)
{
        if (dentry) {                                                                                                                                                                                
        	list_del(&dentry->d_hash);
        	INIT_LIST_HEAD(&dentry->d_hash);
        	dput(dentry); 	
	}
}
static struct inode *sm_create_inode(struct super_block *sb,
				     struct inode *cache_inode) 
{
	struct inode *inode;

	inode = new_inode(sb);
	if (inode) {
		/*FIXME there are still some 
		 * other attributes need to
		 * duplicated*/
		inode->i_ino = cache_inode->i_ino;	 		
		inode->i_mode = cache_inode->i_mode;	 		
	}	
	I2CI(inode) = cache_inode;	
	return inode;
}
                                                                                                                                                                                                     
static void prepare_parent_dentry(struct dentry *dentry, struct inode *inode)
{
        atomic_set(&dentry->d_count, 1);
        dentry->d_vfs_flags = 0;
        dentry->d_flags = 0;
        dentry->d_inode = inode;
        dentry->d_op = NULL;
        dentry->d_fsdata = NULL;
        dentry->d_mounted = 0;
        INIT_LIST_HEAD(&dentry->d_hash);
        INIT_LIST_HEAD(&dentry->d_lru);
        INIT_LIST_HEAD(&dentry->d_subdirs);
        INIT_LIST_HEAD(&dentry->d_alias);
}

static int smfs_create(struct inode *dir, 
		       struct dentry *dentry, 
		       int mode)
{
	struct	inode *cache_dir; 
	struct	inode *cache_inode, *inode;
	struct  dentry tmp; 
	struct  dentry *cache_dentry;
	int 	rc;
	
	ENTRY;
	
	cache_dir = I2CI(dir);
        if (!cache_dir)
                RETURN(-ENOENT);
       
	prepare_parent_dentry(&tmp, cache_dir);      
	cache_dentry = d_alloc(&tmp, &dentry->d_name);
        
	if (!cache_dentry) 
                RETURN(-ENOENT);
	
	if(cache_dir && cache_dir->i_op->create)
		rc = cache_dir->i_op->create(cache_dir, cache_dentry, mode);
	if (rc)
		GOTO(exit, rc);
 
	cache_inode = cache_dentry->d_inode;
	
//	inode = sm_create_inode(dir->i_sb, cache_inode);	
	inode = iget(dir->i_sb, cache_inode->i_ino);	

	if (!inode) 
		GOTO(exit, rc = -ENOMEM);
		
	d_instantiate(dentry, inode);	
	
	sm_set_inode_ops(cache_inode, inode);
exit:
	d_unalloc(cache_dentry);	
	RETURN(rc);
}

static struct dentry *smfs_lookup(struct inode *dir,
		       		  struct dentry *dentry)
{
	struct	inode *cache_dir; 
	struct	inode *cache_inode, *inode;
	struct  dentry tmp; 
	struct  dentry *cache_dentry;
	struct  dentry *rc = NULL;
	
	ENTRY;
	
	cache_dir = I2CI(dir);
        if (!cache_dir)
                RETURN(ERR_PTR(-ENOENT));
	prepare_parent_dentry(&tmp, cache_dir);      
	cache_dentry = d_alloc(&tmp, &dentry->d_name);
      
	if (!cache_dentry)
		RETURN(ERR_PTR(-ENOENT));

	if(cache_dir && cache_dir->i_op->lookup)
		rc = cache_dir->i_op->lookup(cache_dir, cache_dentry);

	if (rc || !cache_dentry->d_inode || 
            is_bad_inode(cache_dentry->d_inode) ||
	    IS_ERR(cache_dentry->d_inode)) {
		GOTO(exit, rc);	
	}

	cache_inode = cache_dentry->d_inode;
	
	inode = iget(dir->i_sb, cache_inode->i_ino);	
		
	d_add(dentry, inode);	
exit:
	d_unalloc(cache_dentry);	
	RETURN(rc);
}		       

static int smfs_lookup_raw(struct inode *dir, const char *name,
                           int len, ino_t *data)
{
	struct	inode *cache_dir; 
	int	rc = 0;

	cache_dir = I2CI(dir);

	if (!cache_dir) 
                RETURN(-ENOENT);
	
	if (cache_dir->i_op->lookup_raw)
		rc = cache_dir->i_op->lookup_raw(cache_dir, name, len, data);		
		
	RETURN(rc);
}

static int smfs_link(struct dentry * old_dentry,
                     struct inode * dir, struct dentry *dentry)
{
	struct	inode *cache_old_inode = NULL; 
	struct	inode *cache_dir = I2CI(dir); 
	struct	inode *inode = NULL; 
	struct  dentry *cache_dentry = NULL;
	struct  dentry tmp; 
	struct  dentry tmp_old; 
	int	rc = 0;

	inode = old_dentry->d_inode;
	
	cache_old_inode = I2CI(inode);
	
	if (!cache_old_inode || !dir) 
                RETURN(-ENOENT);

	prepare_parent_dentry(&tmp_old, cache_old_inode);

	prepare_parent_dentry(&tmp, cache_dir);
	cache_dentry = d_alloc(&tmp, &dentry->d_name); 
 	
	if (cache_dir->i_op->link)
		rc = cache_dir->i_op->link(&tmp, cache_dir, cache_dentry);		
	
	if (rc == 0) {
		d_instantiate(dentry, inode);
	} 	
	
	d_unalloc(cache_dentry);
	
	RETURN(rc);
}
static int smfs_unlink(struct inode * dir, 
		       struct dentry *dentry)
{
	struct inode *cache_dir = I2CI(dir);
	struct inode *cache_inode = I2CI(dentry->d_inode);
	struct dentry *cache_dentry = NULL;
	struct dentry tmp; 
	int    rc = 0;

	if (!cache_dir || !cache_inode)
		RETURN(-ENOENT);
	
	prepare_parent_dentry(&tmp, cache_dir);
	cache_dentry = d_alloc(&tmp, &dentry->d_name); 
	d_add(cache_dentry, cache_inode);
	
	if (cache_inode->i_op->unlink)
		rc = cache_dir->i_op->unlink(cache_dir, cache_dentry);
	
	duplicate_inode(tmp.d_inode, dentry->d_inode);
	
	d_unalloc(cache_dentry);	
	RETURN(rc);	
}
static int smfs_symlink (struct inode * dir,
                	 struct dentry *dentry, 
			 const char * symname)
{
	struct inode *cache_dir = I2CI(dir);
	struct inode *cache_inode = NULL;
	struct inode *inode = NULL;
	struct dentry *cache_dentry = NULL; 
	struct dentry tmp; 
	int    rc = 0;

	if (!cache_dir) 
		RETURN(-ENOENT);
	
	prepare_parent_dentry(&tmp, NULL);
	cache_dentry = d_alloc(&tmp, &dentry->d_name); 

	if (cache_inode->i_op->symlink)
		rc = cache_dir->i_op->symlink(cache_dir, cache_dentry, symname);
	
	cache_inode = cache_dentry->d_inode;
	
	inode = iget(dir->i_sb, cache_inode->i_ino);

	if (inode)
		d_instantiate(dentry, inode);
	else
		rc = -ENOENT;
	
	d_unalloc(cache_dentry);
	RETURN(rc);			
}
static int smfs_mkdir(struct inode * dir, 
		      struct dentry * dentry, 
		      int mode)
{
	struct inode *cache_dir = I2CI(dir);
	struct inode *cache_inode = NULL;
	struct inode *inode = NULL;
	struct dentry *cache_dentry = NULL;
	struct dentry tmp;
	int    rc = 0;

	if (!cache_dir) 
		RETURN(-ENOENT);
	
	prepare_parent_dentry(&tmp, NULL);
	cache_dentry = d_alloc(&tmp, &dentry->d_name); 
	
	if (cache_dir->i_op->mkdir)
		rc = cache_dir->i_op->mkdir(cache_dir, cache_dentry, mode);

	cache_inode = cache_dentry->d_inode;

	inode = iget(dir->i_sb, cache_inode->i_ino);

	if (!inode)
		GOTO(exit, rc = -ENOENT);
 
	d_instantiate(dentry, inode);	
	duplicate_inode(cache_dir, dir);
exit:
	d_unalloc(cache_dentry);
	RETURN(rc);		
}
static int  smfs_rmdir(struct inode * dir, 
		       struct dentry *dentry) 
{
	struct inode *cache_dir = I2CI(dir);
	struct dentry *cache_dentry = NULL;
	struct dentry tmp;
	int    rc = 0;

	if (!cache_dir) 
		RETURN(-ENOENT);
	
	prepare_parent_dentry(&tmp, NULL);
	cache_dentry = d_alloc(&tmp, &dentry->d_name); 
	
	if (cache_dir->i_op->rmdir)
		rc = cache_dir->i_op->rmdir(cache_dir, cache_dentry);

	duplicate_inode(cache_dir, dir);
	duplicate_inode(cache_dentry->d_inode, dentry->d_inode);

	d_unalloc(cache_dentry);
	RETURN(rc);		
}

static int smfs_mknod(struct inode * dir, struct dentry *dentry,
                      int mode, int rdev)
{
	struct inode *cache_dir = I2CI(dir);
	struct dentry *cache_dentry = NULL;
	struct dentry tmp;
	int    rc = 0;

	if (!cache_dir) 
		RETURN(-ENOENT);

	prepare_parent_dentry(&tmp, NULL);
	cache_dentry = d_alloc(&tmp, &dentry->d_name); 
		
	if (cache_dir->i_op->mknod)
		rc = cache_dir->i_op->mknod(cache_dir, dentry, mode, rdev);

	duplicate_inode(cache_dir, dir);
	duplicate_inode(cache_dentry->d_inode, dentry->d_inode);

	d_unalloc(cache_dentry);
	RETURN(rc);		
}
static int smfs_rename(struct inode * old_dir, struct dentry *old_dentry,
                       struct inode * new_dir,struct dentry *new_dentry)
{
	struct inode *cache_old_dir = I2CI(old_dir);
	struct inode *cache_new_dir = I2CI(new_dir);
	struct inode *cache_old_inode = I2CI(old_dentry->d_inode);
	struct inode *cache_new_inode = NULL;
	struct inode *new_inode = NULL;
	struct dentry *cache_old_dentry = NULL;
	struct dentry *cache_new_dentry = NULL;
	struct dentry tmp_new;
	struct dentry tmp_old;
	int    rc = 0;

	if (!cache_old_dir || !cache_new_dir || !cache_old_inode) 
		RETURN(-ENOENT);
	
	prepare_parent_dentry(&tmp_old, old_dir);
	cache_old_dentry = d_alloc(&tmp_old, &old_dentry->d_name); 
	d_add(cache_old_dentry, cache_old_inode);

	prepare_parent_dentry(&tmp_new, NULL);
	cache_new_dentry = d_alloc(&tmp_new, &new_dentry->d_name); 
	
	if (cache_old_dir->i_op->rename)
		rc = cache_old_dir->i_op->rename(cache_old_dir, cache_old_dentry,
					         cache_new_dir, cache_new_dentry);
	
	cache_new_inode = cache_new_dentry->d_inode; 
	new_inode = iget(new_dir->i_sb, cache_new_inode->i_ino);
	
	d_instantiate(new_dentry, new_inode);
		
	duplicate_inode(cache_old_dir, old_dir);
	duplicate_inode(cache_new_dir, new_dir);

	d_unalloc(cache_old_dentry);
	d_unalloc(cache_new_dentry);

	RETURN(rc);		
}

struct inode_operations smfs_dir_iops = {
	create:		smfs_create,
	lookup:		smfs_lookup,
	lookup_raw:     smfs_lookup_raw,        /* BKL held */
        link:           smfs_link,              /* BKL held */
        unlink:         smfs_unlink,            /* BKL held */
        symlink:        smfs_symlink,           /* BKL held */
        mkdir:          smfs_mkdir,             /* BKL held */
        rmdir:          smfs_rmdir,             /* BKL held */
        mknod:          smfs_mknod,             /* BKL held */
        rename:         smfs_rename,            /* BKL held */
        setxattr:       smfs_setxattr,          /* BKL held */
        getxattr:       smfs_getxattr,          /* BKL held */
        listxattr:      smfs_listxattr,         /* BKL held */
        removexattr:    smfs_removexattr,       /* BKL held */
};

static ssize_t smfs_read_dir(struct file *filp, char *buf, 
			     size_t size, loff_t *ppos)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *cache_inode = NULL;
        struct  file open_file;
	struct  dentry open_dentry;
	int    rc = 0;
	
	cache_inode = I2CI(dentry->d_inode);

	if (!cache_inode) 
		RETURN(-EINVAL);

	smfs_prepare_cachefile(dentry->d_inode, filp, cache_inode, 
			       &open_file, &open_dentry);
	
	if (cache_inode->i_fop->read)
		rc = cache_inode->i_fop->read(&open_file, buf, size, ppos);

	smfs_update_file(filp, &open_file);	
	RETURN(rc);	
}

static int smfs_readdir(struct file * filp,
                        void * dirent, 
			filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct inode *cache_inode = NULL;
        struct  file open_file;
	struct  dentry open_dentry;
	int    rc = 0;
	
	cache_inode = I2CI(dentry->d_inode);

	if (!cache_inode) 
		RETURN(-EINVAL);

	smfs_prepare_cachefile(dentry->d_inode, filp, cache_inode, 
			       &open_file, &open_dentry);
	
	if (cache_inode->i_fop->readdir)
		rc = cache_inode->i_fop->readdir(&open_file, dirent, filldir);
	
	smfs_update_file(filp, &open_file);	
	RETURN(rc);	
}

struct file_operations smfs_dir_fops = {
	read:   	smfs_read_dir,	
        readdir:        smfs_readdir,           /* BKL held */
        ioctl:          smfs_ioctl,             /* BKL held */
        fsync:          smfs_fsync,         /* BKL held */
};
