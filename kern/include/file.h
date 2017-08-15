/*
 * Declarations for file handle and file table management.
 */

#ifndef _FILE_H_
#define _FILE_H_

/*
 * Contains some file-related maximum length constants
 */
#include <limits.h>

/*
---------------------
- Filetable setup
---------------------
*/
/*
0	Standard input	STDIN_FILENO	stdin
1	Standard output	STDOUT_FILENO	stdout
2	Standard error	STDERR_FILENO	stderr
*/

/*
open_file_table -> open_file_table_array -> file_descriptor_table
*/
struct file_descriptor {
    struct vnode *v_node;
    struct lock *my_lock;
    char filename[NAME_MAX];
    off_t offset;
    int flag; //read/write/read&write
    int count;
};

struct open_file_table {
    struct file_descriptor * files[OPEN_MAX];
    int count;
};

void file_bootstrap (void);
struct file_descriptor *fd_create(const char *lockName);
void fd_destroy(int fd);
struct open_file_table *oft_create(void);
int oft_insert(struct file_descriptor *fd, struct open_file_table*oft);

/*
---------------------
- Error handling
---------------------
*/
/*contains error returns from /kern/errno.h*/
//not sure if we need this?
int file_errno;

/*
---------------------
- System calls
---------------------
*/


int sys_open(const char *filename, int flags, int mode, int *retval);
int sys_close(int fd);
int sys_read(int fd, void *buf, size_t size, int *retval);
int sys_write(int fd, const void *buf, size_t size, int *retval);
int sys_lseek(int fd, off_t pos, int switch_code, off_t *retval);
int sys_dup2(int fd, int new_fd, int *retval);

#endif /* _FILE_H_ */
