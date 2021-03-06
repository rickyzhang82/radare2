/* radare2 - LGPL - Copyright 2017 - condret */

#include <r_io.h>

R_API int r_io_fd_open(RIO *io, const char *uri, int flags, int mode) {
	RIODesc *desc = r_io_desc_open (io, uri, flags, mode);
	return desc ? desc->fd : -1;
}

R_API bool r_io_fd_close(RIO *io, int fd) {
	return r_io_desc_close (r_io_desc_get (io, fd));
}


//returns length of read bytes
R_API int r_io_fd_read(RIO *io, int fd, ut8 *buf, int len) {
	RIODesc *desc;
	if (!io || !buf || (len < 1) || !(desc = r_io_desc_get (io, fd))) {
		return 0;
	}
	return r_io_desc_read (desc, buf, len);
}

//returns length of written bytes
R_API int r_io_fd_write(RIO *io, int fd, ut8 *buf, int len) {
	RIODesc *desc;
	if (!io || !buf || (len < 1) || !(desc = r_io_desc_get (io, fd))) {
		return 0;
	}
	return r_io_desc_write (desc, buf, len);
}

R_API ut64 r_io_fd_seek(RIO *io, int fd, ut64 addr, int whence) {
	if (!io) {
		return (ut64)-2;
	}
	return r_io_desc_seek (r_io_desc_get (io, fd), addr, whence);
}

R_API ut64 r_io_fd_size(RIO *io, int fd) {
	return r_io_desc_size (r_io_desc_get (io, fd));
}

R_API bool r_io_fd_is_blockdevice(RIO *io, int fd) {
	return r_io_desc_is_blockdevice (r_io_desc_get (io, fd));
}

//returns length of read bytes
R_API int r_io_fd_read_at(RIO *io, int fd, ut64 addr, ut8 *buf, int len) {
	RIODesc *desc;
	if (!io || !buf || (len < 1) || !(desc = r_io_desc_get (io, fd))) {
		return 0;
	}
	return r_io_desc_read_at (desc, addr, buf, len);
}

//returns length of written bytes
R_API int r_io_fd_write_at(RIO *io, int fd, ut64 addr, ut8 *buf, int len) {
	RIODesc *desc;
	if (!io || !buf || (len < 1) || !(desc = r_io_desc_get (io, fd))) {
		return 0;
	}
	return r_io_desc_write_at (desc, addr, buf, len);
}

R_API int r_io_fd_get_pid(RIO *io, int fd) {
	RIODesc *desc;
	if (!io || !io->files) {
		return -2;
	}
 	desc = r_io_desc_get (io, fd);
	return r_io_desc_get_pid (desc);
}

R_API int r_io_fd_get_tid(RIO *io, int fd) {
	RIODesc *desc;
	if (!io || !io->files) {
		return -2;
	}
 	desc = r_io_desc_get (io, fd);
	return r_io_desc_get_tid (desc);
}

R_API bool r_io_use_fd(RIO* io, int fd) {
	if (!io || !io->desc) {
		return false;
	}
	if (io->desc->fd != fd) {
		RIODesc* desc;
		//update io->desc if fd is not the same
		if (!(desc = r_io_desc_get (io, fd))) {
			return false;
		}
		io->desc = desc;
	}
	return true;
}
