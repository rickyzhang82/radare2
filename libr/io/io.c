/* radare2 - LGPL - Copyright 2008-2017 - condret, pancake, alvaro_fe */

#include <r_io.h>
#include <sdb.h>
#include <config.h>

R_LIB_VERSION (r_io);

typedef int (*cbOnIterMap) (RIO *io, int fd, ut64 vaddr, ut8 *buf, int len);
static int onIterMap(SdbListIter *iter, RIO *io, ut64 vaddr, ut8 *buf,
		int len, int match_flg, cbOnIterMap op) {
	ut64 vendaddr = vaddr + len;
	int t, ret = 0;
	for (; iter; iter = iter->p) {
		RIOMap *map = (RIOMap *)iter->data;
		if (vaddr < map->to && map->from < vendaddr) {
			if ((map->flags & match_flg) == match_flg || io->p_cache) {
				RIODesc *desc = io->desc;
				r_io_use_fd (io, map->fd);
				t = vaddr < map->from
					? op (io, map->fd, map->delta, buf + map->from - vaddr,
								R_MIN (vendaddr - map->from, map->to - map->from))
					: op (io, map->fd, map->delta + vaddr - map->from, buf,
								R_MIN (map->to - vaddr, len));
				io->desc = desc;
				if (t < 0) {
					return t;
				}
				ret += t;
			}
			if (vaddr < map->from) {
				t = onIterMap (iter->p, io, vaddr, buf, map->from - vaddr, match_flg, op);
				if (t < 0) {
					return t;
				}
				ret += t;
			}
			if (map->to < vendaddr) {
				t = onIterMap (iter->p, io, map->to, buf + map->to - vaddr, vendaddr - map->to, match_flg, op);
				if (t < 0) {
					return t;
				}
				ret += t;
			}
			break;
		}
	}
	return ret;
}

R_API RIO* r_io_new() {
	return r_io_init (R_NEW0 (RIO));
}

R_API RIO* r_io_init(RIO* io) {
	if (!io) {
		return NULL;
	}
	r_io_desc_init (io);
	r_io_map_init (io);
	r_io_section_init (io);
	r_io_cache_init (io);
	r_io_plugin_init (io);
	r_io_undo_init (io);
	return io;
}

R_API RBuffer *r_io_read_buf(RIO *io, ut64 addr, int len) {
	RBuffer *b = R_NEW0 (RBuffer);
	if (!b) return NULL;
	b->buf = malloc (len);
	if (!b->buf) {
		free (b);
		return NULL;
	}
	len = r_io_read_at (io, addr, b->buf, len);
	b->length = (len < 0)? 0: len;
	return b;
}

R_API int r_io_write_buf(RIO *io, struct r_buf_t *b) {
	return r_io_write_at (io, b->base, b->buf, b->length);
}

R_API void r_io_free(RIO *io) {
	if (!io) {
		return;
	}
	r_io_fini (io);
	r_cache_free (io->buffer);
	free (io);
}

R_API RIODesc *r_io_open_as(RIO *io, const char *urihandler, const char *file, int flags, int mode) {
	RIODesc *ret;
	char *uri;
	int urilen, hlen = strlen (urihandler);
	urilen = hlen + strlen (file) + 5;
	uri = malloc (urilen);
	if (!uri)
		return NULL;
	if (hlen > 0)
		snprintf (uri, urilen, "%s://%s", urihandler, file);
	else strncpy (uri, file, urilen);
	ret = r_io_open_nomap (io, uri, flags, mode);
	free (uri);
	return ret;
}

R_API RIODesc *r_io_open_nomap(RIO *io, const char *uri, int flags, int mode) {
	RIODesc *desc;
	if (!io) {
		return NULL;
	}
	desc = r_io_desc_open (io, uri, flags, mode);
	if ((io->autofd || !io->desc) && desc) {
		io->desc = desc;
	}
	//set desc as current if autofd or io->desc==NULL
	return desc;
}

/* opens a file and maps it to 0x0 */
R_API RIODesc* r_io_open(RIO* io, const char* uri, int flags, int mode) {
	RIODesc* desc;
	if (!io || !io->maps) {
		return NULL;
	}
	desc = r_io_open_nomap (io, uri, flags, mode);
	if (!desc) {
		return NULL;
	}
	r_io_map_new (io, desc->fd, desc->flags, 0LL, 0LL, r_io_desc_size (desc));
	return desc;
}

/* opens a file and maps it to an offset specified by the "at"-parameter */
R_API RIODesc* r_io_open_at(RIO* io, const char* uri, int flags, int mode, ut64 at) {
	RIODesc* desc;
	ut64 size;
	if (!io || !io->maps) {
		return NULL;
	}
	desc = r_io_open_nomap (io, uri, flags, mode);
	if (!desc) {
		return NULL;
	}
	size = r_io_desc_size (desc);
	// second map
	if (size && ((UT64_MAX - size + 1) < at)) { 
		// split map into 2 maps if only 1 big map results into interger overflow
		r_io_map_new (io, desc->fd, desc->flags, UT64_MAX - at + 1, 0LL, size - (UT64_MAX - at) - 1);
		// someone pls take a look at this confusing stuff
		size = UT64_MAX - at + 1; 
	}
	r_io_map_new (io, desc->fd, desc->flags, 0LL, at, size); // first map
	return desc;
}

/* opens many files, without mapping them. This should be discussed */
R_API RList* r_io_open_many(RIO* io, const char* uri, int flags, int mode) {
	RList* desc_list;
	RListIter* iter;
	RIODesc* desc;
	if (!io || !io->files || !uri) {
		return NULL;
	}
	RIOPlugin* plugin = r_io_plugin_resolve (io, uri, 1);
	if (!plugin || !plugin->open_many || !plugin->close) {
		return NULL;
	}
	if (!(desc_list = plugin->open_many (io, uri, flags, mode))) {
		return NULL;
	}
	r_list_foreach (desc_list, iter, desc) {
		if (desc) {
			if (!desc->plugin) {
				desc->plugin = plugin;
			}
			if (!desc->uri) {
				desc->uri = strdup (uri);
			}
			//should autofd be honored here?
			r_io_desc_add (io, desc);
			if (!io->desc) {                                        
				io->desc = desc;
			}
		}
	}
	return desc_list;
}

R_API bool r_io_reopen(RIO* io, int fd, int flags, int mode) {
	RIODesc	*old, *new;
	char *uri;
	if (!(old = r_io_desc_get (io, fd))) {
		return false;
	}
	//does this really work, or do we have to handler debuggers ugly
	uri = old->referer? old->referer: old->uri;
	if (!(new = r_io_open_nomap (io, uri, flags, mode))) {
		return false;
	}
	r_io_desc_exchange (io, old->fd, new->fd);
	return r_io_desc_close (old); // magic
}

R_API int r_io_close_all(RIO* io) { // what about undo?
	if (!io) {
		return false;
	}
	r_io_desc_fini (io);
	r_io_map_fini (io);
	r_io_section_fini (io);
	ls_free (io->plugins);
	r_list_free (io->cache);
	r_io_desc_init (io);
	r_io_map_init (io);
	r_io_section_init (io);
	r_io_cache_init (io);
	r_io_plugin_init (io);
	return true;
}

R_API int r_io_pread_at(RIO* io, ut64 paddr, ut8* buf, int len) {
	if (!io || !buf || len < 1) {
		return 0;
	}
	if (io->ff) {
		memset (buf, 0xff, len);
	}
	return r_io_desc_read_at (io->desc, paddr, buf, len);
}

R_API int r_io_pwrite_at(RIO* io, ut64 paddr, const ut8* buf, int len) {
	if (!io) {
		return 0;
	}
	return r_io_desc_write_at (io->desc, paddr, buf, len);
}

R_API int r_io_vread_at(RIO* io, ut64 vaddr, ut8* buf, int len) {
	if (!io || !buf || len < 0) {
		return -1;
	}
	if (io->ff) {
		memset (buf, 0xff, len);
	}
	r_io_map_cleanup (io);
	if (!io->maps) {
		return r_io_pread_at (io, vaddr, buf, len);
	}
	if (io->ff) {
		memset (buf, 0xff, len);
	}
	return onIterMap (io->maps->tail, io, vaddr, buf, len, R_IO_READ, r_io_fd_read_at);
}

R_API int r_io_vwrite_at(RIO* io, ut64 vaddr, const ut8* buf, int len) {
	if (!io || !buf || len < 0) {
		return -1;
	}
	r_io_map_cleanup (io);
	if (!io->maps) {
		return r_io_pwrite_at (io, vaddr, buf, len);
	}
	return onIterMap (io->maps->tail, io, vaddr, (ut8*)buf, len, R_IO_WRITE, (cbOnIterMap)r_io_fd_write_at);
}

R_API int r_io_read_at(RIO* io, ut64 addr, ut8* buf, int len) {
	if (!io || !buf || len < 0) {
		return -1;
	}
	if (io->buffer_enabled) {
		return r_io_buffer_read (io, addr, buf, len);
	}
	int ret = io->va
		? r_io_vread_at (io, addr, buf, len)
		: r_io_pread_at (io, addr, buf, len);
	if (io->cached_read) {
		(void)r_io_cache_read (io, addr, buf, len);
	}
	return ret;
}

R_API bool r_io_read_all_at(RIO* io, ut64 addr, ut8* buf, int len) {
	return r_io_read_at (io, addr, buf, len) == len;
}

R_API int r_io_write_at(RIO* io, ut64 addr, const ut8* buf, int len) {
	int i, ret = 0;
	ut8 *mybuf = (ut8*)buf;
	if (!io || !buf || len < 0) {
		return -1;
	}
	if (io->write_mask) {
		mybuf = r_mem_dup ((void*)buf, len);
		for (i = 0; i < len; i++) {
			//this sucks
			mybuf[i] &= io->write_mask[i % io->write_mask_len];
		}
	}
	if (io->cached) {
		ret = r_io_cache_write (io, addr, mybuf, len);
	} else if (io->va) {
		ret = r_io_vwrite_at (io, addr, mybuf, len);
	} else {
		ret = r_io_pwrite_at (io, addr, mybuf, len);
	}
	if (buf != mybuf) {
		free (mybuf);
	}
	return ret;
}

R_API bool r_io_write_all_at(RIO* io, ut64 addr, const ut8* buf, int len) {
	return r_io_write_at (io, addr, buf, len) == len;
}

R_API int r_io_read(RIO* io, ut8* buf, int len) {
	if (!io) {
		return -1;
	}
	int ret = r_io_read_at (io, io->off, buf, len);
	if (ret > 0) {
		io->off += ret;
	}
	return ret;
}

R_API int r_io_write(RIO* io, ut8* buf, int len) {
	if (!io) {
		return -1;
	}
	int ret = r_io_write_at (io, io->off, buf, len);
	if (ret > 0) {
		io->off += ret;
	}
	return ret;
}

R_API ut64 r_io_size(RIO* io) {
#pragma message ("rethink this, maybe not needed")
	return io? r_io_desc_size (io->desc): 0LL;
}

R_API bool r_io_is_listener(RIO* io) {
	if (io && io->desc && io->desc->plugin && io->desc->plugin->listener) {
		return io->desc->plugin->listener (io->desc);
	}
	return false;
}

R_API int r_io_system(RIO* io, const char* cmd) {
	if (io && io->desc && io->desc->plugin && io->desc->plugin->system) {
		return io->desc->plugin->system (io, io->desc, cmd);
	}
	return -1;
}

R_API bool r_io_resize(RIO* io, ut64 newsize) {
	if (io && io->desc && io->desc->plugin && io->desc->plugin->resize) {
		bool ret = io->desc->plugin->resize (io, io->desc, newsize);
		if (io->p_cache) {
			r_io_desc_cache_cleanup (io->desc);
		}
		return ret;
	}
	return false;
}

R_API bool r_io_close(RIO *io) {
	return io ? r_io_desc_close (io->desc) : false;
}

R_API int r_io_extend_at(RIO* io, ut64 addr, ut64 size) {
	ut64 cur_size, tmp_size;
	ut8* buffer;
	if (!io || !io->desc || !io->desc->plugin || !size) {
		return false;
	}
	if (io->desc->plugin->extend) {
		int ret;
		ut64 cur_off = io->off;
		r_io_seek (io, addr, R_IO_SEEK_SET);
		ret = io->desc->plugin->extend (io, io->desc, size);
		//no need to seek here
		io->off = cur_off;                              
		return ret;
	}
	if ((io->desc->flags & R_IO_RW) != R_IO_RW) {
		return false;
	}
	cur_size = r_io_desc_size (io->desc);
	if (addr > cur_size) {
		return false;
	}
	if ((UT64_MAX - size) < cur_size) {
		return false;
	}
	if (!r_io_resize (io, cur_size + size)) {
		return false;
	}
	if ((tmp_size = cur_size - addr) == 0LL) {
		return true;
	}
	if (!(buffer = malloc ((size_t) tmp_size + 1))) {
		return false;
	}
	r_io_pread_at (io, addr, buffer, (int) tmp_size);
	r_io_pwrite_at (io, addr + size, buffer, (int) tmp_size);
	free (buffer);
	return true;
}

R_API bool r_io_set_write_mask(RIO* io, const ut8* mask, int len) {
	if (!io || len < 1) {
		return false;
	}
	free (io->write_mask);
	if (!mask) {
		io->write_mask = NULL;
		io->write_mask_len = 0;
		return true;
	}
	io->write_mask = (ut8*) malloc (len);
	memcpy (io->write_mask, mask, len);
	io->write_mask_len = len;
	return true;
}

R_API int r_io_bind(RIO* io, RIOBind* bnd) {
	if (!io || !bnd) {
		return false;
	}
	bnd->io = io;
	bnd->init = true;
	bnd->desc_use = r_io_use_fd;
	bnd->desc_get = r_io_desc_get;
	bnd->desc_size = r_io_desc_size;
	bnd->open = r_io_open_nomap;
	bnd->open_at = r_io_open_at;
	bnd->close = r_io_fd_close;
	bnd->read_at = r_io_read_at;
	bnd->write_at = r_io_write_at;
	bnd->system = r_io_system;
	bnd->is_valid_offset = r_io_is_valid_real_offset;
	bnd->sections_vget = r_io_sections_vget;
	bnd->section_add = r_io_section_add;
	return true;
}

/* moves bytes up (+) or down (-) within the specified range */
R_API int r_io_shift(RIO* io, ut64 start, ut64 end, st64 move) {
	ut8* buf;
	ut64 chunksize = 0x10000;
	ut64 src, shiftsize = r_num_abs (move);
	if (!shiftsize || (end - start) <= shiftsize) {
		return false;
	}
	ut64 rest = (end - start) - shiftsize;
	if (!(buf = calloc (1, chunksize + 1))) {
		return false;
	}
	if (move > 0) {
		src = end - shiftsize;
	} else {
		src = start + shiftsize;
	}
	while (rest > 0) {
		if (chunksize > rest) {
			chunksize = rest;
		}
		if (move > 0) {
			src -= chunksize;
		}
		r_io_read_at (io, src, buf, chunksize);
		r_io_write_at (io, src + move, buf, chunksize);
		if (move < 0) {
			src += chunksize;
		}
		rest -= chunksize;
	}
	free (buf);
	return true;
}

R_API int r_io_create(RIO* io, const char* file, int mode, int type) {
	if (io && io->desc && io->desc->plugin && io->desc->plugin->create) {
		return io->desc->plugin->create (io, file, mode, type);
	}
	if (type == 'd' || type == 1) {
		return r_sys_mkdir (file);
	}
	return r_sandbox_creat (file, mode);
}

R_API ut64 r_io_seek(RIO* io, ut64 offset, int whence) {
	if (!io) {
		return -1;
	}
	ut64 t;
	switch (whence) {
	case R_IO_SEEK_SET:
		io->off = offset;
		break;
	case R_IO_SEEK_CUR:
		io->off += offset;
		break;
	case R_IO_SEEK_END:
	default:
		if (!io->desc || (t = r_io_desc_size (io->desc)) == -1) {
			return -1;
		}
		io->off = t + offset;
		break;
	}
	return io->off;
}

//remove all descs and maps
R_API int r_io_fini(RIO* io) {
	if (!io) {
		return false;
	}
	r_io_desc_cache_fini_all (io);
	r_io_desc_fini (io);
	r_io_map_fini (io);
	r_io_section_fini (io);
	ls_free (io->plugins);
	r_list_free (io->cache);
	r_list_free (io->undo.w_list);
	if (io->runprofile) {
		R_FREE (io->runprofile);
	}
	return true;
}
