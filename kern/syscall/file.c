#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <kern/iovec.h>
#include <thread.h>
#include <limits.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>

/*
 * Add your file-related functions here ...
 */

//global open file table
struct open_file_table *gft;


//open a file
int sys_open(const char *filename, int flags, int mode, int *retval)
{
	int err = 0;

	char *fname;
	size_t len;
	fname = (char*) kmalloc(sizeof(char)*PATH_MAX);
	err = copyinstr((const_userptr_t)filename, fname, PATH_MAX,&len);

	if (err) {
		kfree(fname);
		return err;
	}

	struct file_descriptor *fd = fd_create(fname);
	fd->flag = flags;
	struct vnode *vn;

	if (gft->count == OPEN_MAX) {
		return EBADF; /* Bad file number */
	}

	err = vfs_open(fname, flags, mode, &vn);

	if (err) {
		kfree(fname);
		return err;
	}

	//attach vnode to file descriptor
	fd->v_node = vn;

	//insert file descriptor into our global table
	int index = oft_insert(fd, gft);
	if (index == -1) {
		return EBADF;
	}

	//get current process and insert fd * into file table
	struct proc * curr = curthread->t_proc;

	int i;
	for (i=0; i < OPEN_MAX; i++) {
		if (curr->oft[i] == -1) {
			curr->oft[i] = index;
			break;
		}
	}

	kfree(fname);
	*retval = index;

	return 0;
}


//close a file
int sys_close(int fd)
{
	int err = 0;

	if(fd < 0 || fd >= OPEN_MAX){
		return EBADF; /* Bad file number */
	}
	if(gft->files[fd] == NULL){
		return EBADF; /* Bad file number */
		//or EFAULT?? /* Bad memory reference */
	}

	lock_acquire(gft->files[fd]->my_lock);

	//close vnode
	vfs_close(gft->files[fd]->v_node);
	lock_release(gft->files[fd]->my_lock);
	gft->count --;

	fd_destroy(fd);

	struct proc * curr = curthread->t_proc;

	int i;
	for (i=0; i < OPEN_MAX; i++) {
		if (curr->oft[i] == fd) {
			curr->oft[i] = -1;
			break;
		}
	}
	return err;
}


/* read a file */
int sys_read(int fd, void *buf, size_t size, int *retval)
{
	int err = 0;

	if(fd < 0 || fd >= OPEN_MAX){
		return EBADF; /* Bad file number */
	}
	if (gft->files[fd] == NULL) {
		err = ENOENT;
		return err;
	}
	if((gft->files[fd]->flag & O_WRONLY)){
		return EACCES; /* Invalid argument */
		//or EACCES?? /* Permission denied */
	}
	if (buf == NULL){
		return EFAULT;
	}
	struct uio u;
	struct iovec iov;
	char *new_buf = (char*)kmalloc(size);
	uio_kinit (&iov, &u, (void*) new_buf, size, gft->files[fd]->offset, UIO_READ);


	lock_acquire(gft->files[fd]->my_lock);
	err = VOP_READ(gft->files[fd]->v_node, &u);
	lock_release(gft->files[fd]->my_lock);

	if (err) {
		kprintf("read error %d", err);
		kfree(new_buf);
		return err;
	}
	
	copyout((const void*)new_buf, (userptr_t)buf, size);
	kfree(new_buf);

	gft->files[fd]->offset = u.uio_offset;
	*retval = size - u.uio_resid;

	return err;
}


int sys_write(int fd, const void *buf, size_t size, int *retval)
{
	if(fd < 0 || fd >= OPEN_MAX){
		return EBADF; /* Bad file number */
	}
	if(gft->files[fd] == NULL){
		return EBADF; /* Bad file number */
		//or EFAULT?? /* Bad memory reference */
	}
	//check permissions
	if(!((gft->files[fd]->flag & O_WRONLY) || (gft->files[fd]->flag & O_RDWR))){
		return EACCES; /* Invalid argument */
		//or EACCES?? /* Permission denied */
	}
	if (buf == NULL){
		return EFAULT;
	}

	int err = 0;
	struct uio u;
	struct iovec iov;

	void *new_buf = kmalloc(size);
	copyin((userptr_t)buf, new_buf, size);
	uio_kinit (&iov, &u,(void*) new_buf, size, gft->files[fd]->offset, UIO_WRITE);

	lock_acquire(gft->files[fd]->my_lock);
	err = VOP_WRITE(gft->files[fd]->v_node, &u);
	lock_release(gft->files[fd]->my_lock);


	if (err) {
		kfree(new_buf);
		kprintf("write error %d", err);
		return err;
	}

	kfree(new_buf);
	gft->files[fd]->offset = u.uio_offset;
	*retval = size - u.uio_resid;

	return err;
}

int sys_dup2(int fd, int new_fd, int *retval){

	int err;
	//if fd number incorrect
	if(fd < 0 || new_fd < 0){
		return EBADF; //************CHANGE CODE******
	}//

	if(fd >= OPEN_MAX || new_fd >= OPEN_MAX){//defiantly not max open**********
		return EBADF;
	}

	if(gft->files[fd] == NULL){
		return EBADF; /* Bad file number */
		//or EFAULT?? /* Bad memory reference */
	}

	if(fd == new_fd){
		*retval = new_fd;
		return 0;
	}

	if(gft->files[new_fd] != NULL){
		err = sys_close(fd);
		if(err){
			return EBADF;
		}
	}else{
		gft->files[new_fd] = (struct file_descriptor *)kmalloc(sizeof(struct file_descriptor*));
	}

	lock_acquire(gft->files[fd]->my_lock);
	gft->files[new_fd]->v_node = gft->files[fd]->v_node;
	gft->files[new_fd]->offset = gft->files[fd]->offset;
	gft->files[new_fd]->count = gft->files[fd]->count;
	gft->files[new_fd]->flag = gft->files[fd]->flag;
	//strcpy(gft->files[new_fd]->filename, gft->files[fd]->filename);
	gft->files[new_fd]->my_lock = lock_create("dup2 file");


	lock_release(gft->files[fd]->my_lock);
	*retval = new_fd;
	return 0;
}

int sys_lseek(int fd, off_t pos, int switch_code, off_t *retval){
	int err;
	
	if(fd < 0 || fd >= OPEN_MAX){
		return EBADF; /* Bad file number */
	}
	if(gft->files[fd] == NULL){
		return EBADF; /* Bad file number */
		//or EFAULT?? /* Bad memory reference */
	}

	off_t offset;
	struct stat tmp;
	lock_acquire(gft->files[fd]->my_lock);
	
	// err = gft->files[fd]->v_node->vn_ops->vop_stat(gft->files[fd]->v_node, &tmp);

	//err = VOP_ISSEEKABLE(gft->files[fd]->v_node);
	if (!VOP_ISSEEKABLE(gft->files[fd]->v_node)) {
		lock_release(gft->files[fd]->my_lock);
		return EPERM;
	}

	

	switch(switch_code){
		case SEEK_SET:
			offset = pos;
			break;
		case SEEK_CUR:
			offset = gft->files[fd]->offset + pos;
			break;
		case SEEK_END:
			err = gft->files[fd]->v_node->vn_ops->vop_stat(gft->files[fd]->v_node, &tmp);
			if(err){
				lock_release(gft->files[fd]->my_lock);
				return err;
			}
			offset = tmp.st_size + pos;
			break;
		default:
			lock_release(gft->files[fd]->my_lock);
			return EINVAL;
	}

	if(offset < (off_t)0){
		lock_release(gft->files[fd]->my_lock);
		return EINVAL;
	}

	gft->files[fd]->offset = offset;
	*retval = offset;
	lock_release(gft->files[fd]->my_lock);

	return 0;
}

void file_bootstrap ()
{
	gft = oft_create();
	char console1[] = "con:";
	const char *stdin = "stdin";
	const char *stdout = "stdout";
	const char *stderr = "stderr";

	int err = 0;


	// STDIN
	struct file_descriptor *fd1 = fd_create(stdin);
	struct vnode *vn1 = NULL;

	err = vfs_open(console1, O_RDWR, 0664, &vn1);

	if (err) {
		kprintf("vfs_open failed with code %d\n", err);
		//return err;
	}

	fd1->v_node = vn1;
	gft->files[0] = fd1;
	gft->files[0]->flag = O_RDWR;
	gft->files[0]->offset = 0;
	gft->files[0]->my_lock = lock_create(console1);
	gft->files[0]->count = 1;
	gft->count ++;

	//STDOUT
	char console2[] = "con:";
	struct file_descriptor *fd2 = fd_create(stdout);
	struct vnode *vn2 = NULL;

	err = vfs_open(console2, O_RDWR, 0, &vn2);

	if (err) {
		kprintf("vfs_open failed with code %d\n", err);
		//return err;
	}

	fd2->v_node = vn2;
	gft->files[1] = fd2;
	gft->files[1]->flag = O_RDWR;
	gft->files[1]->offset = 0;
	gft->files[1]->my_lock = lock_create(console1);
	gft->files[1]->count = 1;
	gft->count ++;

	//STDERR
	char console3[] = "con:";
	struct file_descriptor *fd3 = fd_create(stderr);
	struct vnode *vn3 = NULL;

	err = vfs_open(console3, O_RDWR, 0664, &vn3);

	if (err) {
		kprintf("vfs_open failed with code %d\n", err);
		//return err;
	}

	fd3->v_node = vn3;
	gft->files[2] = fd3;
	gft->files[2]->flag = O_RDWR;
	gft->files[2]->offset = 0;
	gft->files[2]->my_lock = lock_create(console2);
	gft->files[2]->count = 1;
	gft->count ++;

	//kprintf("gft->")
}

/*
file descriptor and open file table functions
*/

struct file_descriptor *fd_create(const char *lockName) {
	//const char *temp = lockName;
	//temp++;
	struct file_descriptor *f;
	f = kmalloc(sizeof(*f));
	f->v_node = NULL;
	f->my_lock = lock_create(lockName);
	f->offset = 0;
	f->flag = 0;
	f->count = 1;
	return f;
}

void fd_destroy(int fd)
{
	kfree(gft->files[fd]);
	gft->files[fd] = NULL;
}

struct open_file_table *oft_create()
{
	//reserve space for oft
	struct open_file_table *oft;
	oft = kmalloc(sizeof(*oft));
	if (oft == NULL) {
		return NULL;
	}

	//struct fd *f[OPEN_MAX] = kmalloc(sizeof(*fd) * OPEN_MAX)
	int i;
	for (i=0; i<OPEN_MAX; i++) {
		oft->files[i] = NULL;
	}

	oft->count = 0;
	return oft;
}

/*will return -1 if table is full*/
int oft_insert(struct file_descriptor *fd, struct open_file_table*oft)
{
	if (oft->count == OPEN_MAX) {
		return -1;
	}
	//kprintf("oft insert count check fine\n");

	int i;
	for (i=0; i<OPEN_MAX;i++) {
		if (oft->files[i] == NULL) {
			oft->files[i] = fd;
			oft->count ++;
			return i;
		}
	}
	//kprintf("oft insert count check fine\n");

	return 0;
}
