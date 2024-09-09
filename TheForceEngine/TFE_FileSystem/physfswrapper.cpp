/*
 * TFE Wrapper for PHYSFS
 *
 * Virtual Paths for TFE
 *
 * The idea is to use PHYSFS to create a virtual filesystem tree which contains
 * - Application data (UI_Images, ...) at root /tfe/
 * - Current Game Data (*.GOB / *.LAB, ..:) at root /game/
 * - temporary data (i.e. managing mods or game source data) at /tmp/
 *
 * At application startup, the TFE support file (i.e. PNGs, Fonts, soundfonts)
 * that are shipped with TFE are mounted at /tfe to be accessible any time.
 *
 * The User Documents directory is then added to /tfe at lower priority, so that
 * any files with the same names DO NOT override the files shipping with TFE.
 *
 * This has the following advantages:
 * - the TFE support files can be packed into a ZIP file, this ZIP file can
 *   then be appended to the executable, and therefore TFE+its support files
 *   can be contained in a single file!
 * - This file is then physfs mounted to /tfe giving access to all files.
 * - The user documents directory is then overlaid with lower priority, meaning
 *   Mods, Soundfonts, Fonts, the user has selected are also visible at the
 *   same paths, but identically named files do not override the shipped ones.
 *
 * WRITING files is only possible at the user support directory, with no
 *  absolute path support.
 *   consider user dir /home/user/tfe"
 *   vpMkdir("dir1") will create a directory at "/home/user/tfe/dir1"
 *   vpOpenWrite("f1.txt") will create a file at "/home/user/tfe/f1.txt"
 *   vpOpenWrite("dir1/txt.txt") will open a file at "/home/user/tfe/dir1/txt.txt"
 *
 * No other paths allow writing.
 *
 * Unfortunately, we need to work around a few physfs shortcomings, like
 *  mounting/unmounting is based on real archive names, not their virtual
 * mountpoints, which is a problem when e.g. game is running and UI tries to
 * validate the source data string:  you _can_ mount the source multiple times,
 * but unmounting is based on the mount source string so it will unmount all
 * mountpoints in a single physfs_unmount call.
 */

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <physfs.h>
#include <vector>
#include <string>
#include <TFE_FileSystem/fileutil.h>
#include <SDL.h>
#include <SDL_endian.h>
#include "physfswrapper.h"
#include "ignorecase.h"
#include "physfsrwops.h"

//#define DEBUG_PHYSFSWRAPPER
#ifdef DEBUG_PHYSFSWRAPPER
#define PWDBG fprintf
#else
#define PWDBG(...) do {} while (0)
#endif


static const char * const VIRT_PATH_ABS	  = "/";
static const char * const VIRT_PATH_TFE	  = "/tfe/";
static const char * const VIRT_PATH_GAME  = "/game/";
static const char * const VIRT_PATH_TMP   = "/tmp/";
static const char * const VIRT_PATH_TMP2  = "/tmp2/";
static const char * const VIRT_PATH_EDPRJ = "/edit/"; // edit project

static const char * const _vpaths[6] = {
	VIRT_PATH_ABS, VIRT_PATH_TFE, VIRT_PATH_GAME,
	VIRT_PATH_TMP, VIRT_PATH_TMP2, VIRT_PATH_EDPRJ
};


typedef std::vector<std::string> TFEFileList;

// internal structure to keep track of mount points and work around
// (IMO) physfs api misdesign.
struct vpMount {
	TFE_VPATH vp;		// mount root
	char* mntname;		// src name of the mount, for unmounting
	unsigned int id;	// mount counter
	TFEFile f;		// backing file
	unsigned int refcnt;	// refcount for
};


/* internal vars */
static const char *_tfe_userdir = nullptr;
static std::vector<TFEMount> _vpmounts;
static std::atomic_int _vpgenid;

/*
 * we keep track of all mounts we ever made, and do refcounting on them,
 * so we cann dynamically mount/unmount the same source at multiple mountpoints.
 * On unmount, physfs uses the source (not the mount point) to unmount, so
 * things which are mounted at multiple locations will lose _All_ their mount-
 * points on a single unmount call.  We do not want that.
 */
static TFEMount newMount(TFE_VPATH vp, const char *mpath)
{
	TFEMount m = new struct vpMount;
	if (!m)
		return nullptr;
	m->id = _vpgenid++;
	m->vp = vp;
	m->mntname = strdup(mpath);
	m->f = nullptr;
	m->refcnt = 1;
	PWDBG(stderr, "z newMount(%s) = %p\n", mpath, m);
	_vpmounts.push_back(m);
	return m;
}

static void delMount(TFEMount m)
{
	PWDBG(stderr, "z delMount(%s, %d)\n", m->mntname, m->refcnt);
	auto it = _vpmounts.begin();
	while (it != _vpmounts.end()) {
		if (m == *it) {
			if (m->refcnt < 1) {
				free(m->mntname);
				m->mntname = nullptr;
				m->f = nullptr;
				_vpmounts.erase(it);
				// can't free the TFEMount since it may be
				// used by multiple parts of the code,
				// and freeing it here may crash the other part
				// that tries to unmount it.
				// use vpUnmountTree() to free() them all.
				// which you should do when you close DF/OL.
			}
			break;
		}
		it++;
	}
}

static TFEMount findMount(const char *mntname, bool virt)
{
	TFEMount ret = nullptr;

	for (auto i : _vpmounts)
	{
		if (i->refcnt < 1)
			continue;
		if (0 == strcasecmp(mntname, i->mntname)) {
			if (virt && i->f) {
				ret = i;
				break;
			} else if (!virt && !i->f) {
				ret = i;
				return i;
			}
		}
	}
	PWDBG(stderr, "z findMount(%s %d) = %p\n", mntname, virt, ret);
	return ret;
}

/*
 * Initialize vfs
 * @argv0: 	argv[0] of main
 * @userdata:	absolute path to location of per-user data
 *                e.g.. SDL_GetPrefPath(...) 
 * @return	0 on success, or error code.
 */
int vpInit(const char * const argv0, const char * const userdata)
{
	int ret, ret2;
	TFEMount m1, m2;

	if (!argv0 || strlen(argv0) < 1 || !userdata || strlen(userdata) < 1)
			return 98;

	ret  = PHYSFS_init(argv0);
	if (!ret)
		return 97;

	PHYSFS_permitSymbolicLinks(1);

	/* mount argv0 at VPATH_TFE */
	ret = PHYSFS_mount(argv0, _vpaths[VPATH_TFE], 0);
	if (!ret) {
		ret = 96;
		goto out_err1;
	}

	/* check existence of a few files to consider it sane? */
	if (!PHYSFS_exists("/tfe/UI_Images/TFE_TitleLogo.png")) {
		ret = 95;
		goto out_err2;
	}

	/* overlay the userdata dir */
	ret = PHYSFS_mount(userdata, _vpaths[VPATH_TFE], 1);
	if (!ret) {
		ret = 89;
		goto out_err2;
	}

	if (!vpSetWriteDir(userdata))
	{
		ret = 88;
		goto out_err3;
	}

	FileUtil::setCurrentDirectory(userdata); // FIXME: go away!
	_tfe_userdir = userdata;

	m1 = newMount(VPATH_TFE, argv0);
	m2 = newMount(VPATH_TFE, userdata);

	return 0;

out_err3:
	PHYSFS_unmount(userdata);
out_err2:
	PHYSFS_unmount(argv0);
out_err1:
	ret2 = PHYSFS_deinit();
	if (ret2 == 0)
		ret = 99;	/* physfs is .. unusable */

	return ret;
}

static void _vpUnmountTree(TFE_VPATH vpid)
{
	auto it = _vpmounts.begin();
	while (it != _vpmounts.end()) {
		TFEMount m = *it;
		if (m->vp == vpid && m->refcnt > 0) {
			PWDBG(stderr, "z vpUnmountTree(%s)\n", m->mntname);
			free(m->mntname);
			_vpmounts.erase(it);
			int ret = PHYSFS_unmount(m->mntname);
			delete m;
			continue;
		}
		++it;
	}
}

void vpDeinit(void)
{
	// sort in reverse "id" order
	std::sort(_vpmounts.begin(), _vpmounts.end(),
		  [](auto a, auto b) { return (a->id > b->id); });

	_vpUnmountTree(VPATH_TMP2);
	_vpUnmountTree(VPATH_TMP);
	_vpUnmountTree(VPATH_GAME);
	_vpUnmountTree(VPATH_TFE);
	if (!PHYSFS_deinit())
		fprintf(stderr, "x PHYSFS_deinit() failed!\n  reason: %s.\n", PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
}

void vpUnmountTree(TFE_VPATH vpid)
{
	if (vpid == VPATH_TFE)
		return;		// only allowed on shutdown

	std::sort(_vpmounts.begin(), _vpmounts.end(),
		  [](auto a, auto b) { return (a->id > b->id); });

	_vpUnmountTree(vpid);
}

static char *to_vpath(TFE_VPATH v, const char *fn)
{
	const int i = strlen(fn);
	const int j = strlen(_vpaths[v]);
	char *z = (char *)malloc(i + j + 1);
	if (!z)
		return nullptr;
	snprintf(z, i + j + 1, "%s%s", _vpaths[v], fn);
	return z;
}

char* vpToVpath(TFE_VPATH v, const char *fn)
{
	return to_vpath(v, fn);
}

/*
 * does the file "filename" exist in the given vpath
 */
bool vpFileExists(const char* filepath, bool matchcase)
{
	char *fp = strdup(filepath);
	bool found = false;
	int ret;

	if (!matchcase) {
		ret = PHYSFSEXT_locateCorrectCase(fp);
		if (ret != 0) {
			PWDBG(stderr, "x matchcase(%s) err %d\n", fp, ret);
			found = false;
		} else {
			found = true;
		}
	} else {
		ret = PHYSFS_exists(fp);
		if (ret != 0)
			found = true;
	}

	free(fp);
	PWDBG(stderr, "vpFileExists(%s, %d) = %d\n", filepath, matchcase, found);
	return found;
}

bool vpFileExists(TFE_VPATH vpathid, const char* filename, bool matchcase)
{
	char *fp = to_vpath(vpathid, filename);
	bool ret;

	if (!fp)
		return false;

	ret = vpFileExists(fp, matchcase);
	free(fp);
	return ret;
}

bool vpFileExists(TFEMount mnt, const char* filename, bool matchcase)
{
	return vpFileExists(mnt->vp, filename, matchcase);
}

/* vpMkdir("Screenshots")  -> /home/user/.local/TheForceEngine/Screenshots */
bool vpMkdir(const char* dirname)
{
	if (!dirname)
		return false;
	int ret = PHYSFS_mkdir(dirname);
	if (ret == 0) {
		PWDBG(stderr, "x vpMkdir(%s) failed: %s\n", dirname, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	PWDBG(stderr, "vpMkdir(%s): %d\n", dirname, ret);
	return ret != 0;
}

static TFEFile _vpFileOpen(const char* filepath, bool matchcase)
{
	TFEFile f;
	int ret;
	char *fp = strdup(filepath);

	if (!matchcase) {
		ret = PHYSFSEXT_locateCorrectCase(fp);
		if (ret < 0) {
			PWDBG(stderr, "x matchcase(%s) err %d\n", fp, ret);
			ret = 0;
		} else
			ret = 1; // OK
	} else {
		ret = 1;
	}

	f = (ret == 0) ? nullptr : PHYSFS_openRead(fp);

	free(fp);
	return f;
}

TFEFile vpFileOpen(const char* filename, TFE_VPATH vpathid, bool matchcase)
{
	TFEFile f = nullptr;
	char *fp = to_vpath(vpathid, filename);
	if (!fp)
		goto out;

	f = _vpFileOpen(fp, matchcase);
out:
	PWDBG(stderr, "vpFileOpen(%s, %d) = %p\n", fp, matchcase, f);
	if (fp)
		free(fp);
	return f;
}

static SDL_RWops* _vpFileOpenRW(const char* filename, bool matchcase)
{
	SDL_RWops *f;
	int ret;
	char *fp = strdup(filename);

	if (!matchcase) {
		ret = PHYSFSEXT_locateCorrectCase(fp);
		if (ret < 0) {
			PWDBG(stderr, "x matchcase(%s) err %d\n", fp, ret);
			ret = 0;
		} else
			ret = 1; // OK
	} else {
		ret = 1;
	}

	f = (ret == 0) ? nullptr : PHYSFSRWOPS_openRead(fp);

	free(fp);
	return f;
}

SDL_RWops* vpFileOpenRW(const char* filename, TFE_VPATH vpathid, bool matchcase)
{
	SDL_RWops *f = nullptr;
	char *fp = to_vpath(vpathid, filename);
	if (!fp)
		goto out;

	f = _vpFileOpenRW(fp, matchcase);

out:
	PWDBG(stderr, "vpFileOpenRW(%s, %d) = %p\n", fp, matchcase, f);
	if (fp)
		free(fp);
	return f;
}


TFEFile vpOpenWrite(const char* fn, TFE_WMODE wmode)
{
	TFEFile f;

	if (wmode == WMODE_WRITE)
		f = PHYSFS_openWrite(fn);
	else if (wmode == WMODE_APPEND)
		f = PHYSFS_openAppend(fn);
	else
		f = nullptr;

	if (!f) {
		PWDBG(stderr, "x vpOpenWrite(%s) failed: %s\n", fn, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	PWDBG(stderr, "vpOpenWrite(%s) = %p\n", fn, f);
	return f;
}

SDL_RWops* vpOpenWriteRW(const char* fn, TFE_WMODE wmode)
{
	SDL_RWops *f;

	if (wmode == WMODE_WRITE)
		f = PHYSFSRWOPS_openWrite(fn);
	else if (wmode == WMODE_APPEND)
		f = PHYSFSRWOPS_openAppend(fn);
	else
		f = nullptr;

	if (!f) {
		PWDBG(stderr, "vpOpenWriteRW(%s) failed: %s\n", fn, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	PWDBG(stderr, "vpOpenWriteRW(%s) = %p\n", fn, f);
	return f;
}


void vpCloseRW(SDL_RWops *rw)
{
	void *p = rw;
	int ret = (rw) ? SDL_RWclose(rw) : -1;
	if (ret < 0)
	{
		PWDBG(stderr, "x vpClose(RDL_RW %p) failed: %s\n", p, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	PWDBG(stderr, "vpClose(SDLRW %p) %d\n", p, ret);
}

void vpClose(TFEFile f)
{
	int ret = (f) ? PHYSFS_close(f) : -1;
	if (!ret)
	{
		fprintf(stderr, "x vpClose(TFEF %p) failed: %s\n", f, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	PWDBG(stderr, "vpClose(TFEFile %p) = %d\n", f, ret);
}

void vpDeleteFile(const char* filename)
{
	char *fp = to_vpath(VPATH_TFE, filename);
	if (fp) {
		PHYSFS_delete(fp);
		free(fp);
	}
}

// mountReal("/home/dt/xy.zip", VPATH_TMP, true);
// mountReal(const char *src, TFE_VPATH dest, bool front)
// Real Filesystem DIR/Container mount
TFEMount vpMountReal(const char* srcname, TFE_VPATH vpdst, bool front)
{
	TFEMount mnt;
	int ret = 0;

	mnt = findMount(srcname, false);
	if (mnt) {
		++(mnt->refcnt);
		goto done;
	}

	ret = PHYSFS_mount(srcname, _vpaths[vpdst], front ? 0 : 1);
	if (!ret) {
		delMount(mnt);
		PWDBG(stderr, "vpMount2(id:%s) failed: %s\n", srcname, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
		return nullptr;
	}
	mnt = newMount(vpdst, srcname);
	if (!mnt) {
		PHYSFS_unmount(srcname);
		return nullptr;
	}

done:
	PWDBG(stderr, "vpMountReal(%s %d %d) = %p\n", srcname, vpdst, front, mnt);
	return mnt;
}

// mountVirt(VPATH_TMP, "DTIDE3.GOB", VPATH_TMP, true);
// mountVirt(srcroot, srcname, destroot, front)
// container-in-container mount
TFEMount vpMountVirt(TFE_VPATH vpsrc, const char *srcname, TFE_VPATH vpdst, bool front, bool matchcase)
{
	TFEMount mnt = nullptr;
	PHYSFS_File *f = nullptr;
	char *fp = to_vpath(vpsrc, srcname);
	if (!fp)
		goto out0;

	mnt = findMount(fp, true);
	if (mnt) {
		++mnt->refcnt;
		PWDBG(stderr, "vpMountVirt(%s %d %d %d) = existing %p\n", fp, vpdst, front, matchcase, mnt);
		return mnt;
	}
	if (!matchcase) {
		int ret = PHYSFSEXT_locateCorrectCase(fp);
		if (ret < 0) {
			PWDBG(stderr, "x matchcase(%s) err %d\n", fp, ret);
			goto out1;
		}
	}
	f = PHYSFS_openRead(fp);
	if (!f)
		goto out1;

	if (!PHYSFS_mountHandle(f, fp, _vpaths[vpdst], front ? 0 : 1)) {
		fprintf(stderr, "vpMountVirt(id:%s) failed: %s\n", fp, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
		goto out2;
	}

	mnt = newMount(vpdst, fp);
	if (!mnt)
		goto out3;

	mnt->f = f;		// file to close
	PWDBG(stderr, "vpMountVirt(%s %d %d %d %p) = %p\n", fp, vpdst, front, matchcase, f, mnt);
	return mnt;

out3:
	PHYSFS_unmount(srcname);
out2:
	PHYSFS_close(f);
out1:
	free(fp);
out0:
	PWDBG(stderr, "x vpMountVirt(%s) failed:%s\n", srcname, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	return nullptr;
}

bool vpUnmount(TFEMount mnt)
{
	int ret, ok;

	ok = true;
	--mnt->refcnt;
	if (mnt->refcnt < 1) {
		ret = PHYSFS_unmount(mnt->mntname);
		if (!ret) {
			PWDBG(stderr, "vpUnmount: %s: failed: %s\n", mnt->mntname, PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
			ok = false;
		}
		if (mnt->f) {
			vpClose(mnt->f);
			mnt->f = nullptr;
		}
		delMount(mnt);
		free(mnt);
		mnt = nullptr;
	}
	PWDBG(stderr, "vpUnmount(%p) = %d\n", mnt, ok);
	return ok;
}

static bool hasext(const char *fn, const TFEExtList& exts)
{
	int l = strlen(fn);
	if (l < 3)
		return false;
	fn = strrchr(fn, '.');
	if (!fn)
		return false;
	++fn;
	for (auto i : exts)
		if (0 == strcasecmp(fn, i))
			return true;
	return false;
}

static bool vpGetFileList(const char *path, TFEFileList& inout, TFEExtList& te, bool matchcase)
{
	char **ret, **i;
	int do_extf = !te.empty();

	if (!matchcase) {
		char *xp = strdup(path);
		if (!xp)
			return false;
		int lc = PHYSFSEXT_locateCorrectCase(xp);
		if (lc < 0) {
			PWDBG(stderr, "x matchcase(%s) err %d\n", xp, lc);
			return false;
		} else {
			ret = PHYSFS_enumerateFiles(xp);
		}
	} else {
		ret = PHYSFS_enumerateFiles(path);
	}

	if (!ret)
		return false;

	for (i = ret; *i != NULL; i++) {
		if (do_extf) {
			if (hasext(*i, te)) {
				inout.emplace_back(*i);
			}
		} else {
			inout.emplace_back(*i);
		}
	}

	PHYSFS_freeList(ret);

	return true;
}

bool vpGetFileList(TFE_VPATH vpathid, TFEFileList& inout, TFEExtList& te)
{
	return vpGetFileList(_vpaths[vpathid], inout, te, true);
}

bool vpGetFileList(TFE_VPATH vpathid, const char* subpath, TFEFileList& inout, TFEExtList& te, bool matchcase)
{
	char buf[2048];
	memset(buf, 0,2048);
	if (strlen(buf) > 2032)
		return false;
	strcpy(buf, _vpaths[vpathid]);
	strcat(buf, subpath);
	return vpGetFileList(buf, inout, te, matchcase);
}

bool vpDirExists(const char* dirname, TFE_VPATH vpsrc)
{
	PHYSFS_Stat s;
	char *fp = to_vpath(vpsrc, dirname);
	if (!fp)
		return false;

	int rc = PHYSFS_stat(fp, &s);
	if (rc)
		rc = (s.filetype == PHYSFS_FILETYPE_DIRECTORY);

	PWDBG(stderr, "vpDirExists(%s): %d\n", fp, rc);
	free(fp);
	return rc ? true : false;
}

bool vpSetWriteDir(const char *realpath)
{
	int ret = 1;
	if (realpath)
		ret = PHYSFS_setWriteDir(realpath);
	else
		ret = PHYSFS_setWriteDir(_tfe_userdir);

	return (ret != 0);
}

static const char* _vpGetFileContainer(char* fn, bool matchcase)
{
	if (!matchcase) {
		int ret = PHYSFSEXT_locateCorrectCase(fn);
		if (ret < 0) {
			PWDBG(stderr, "x matchcase(%s) err %d\n", fn, ret);
			return nullptr;
		}
	}
	const char *p = PHYSFS_getRealDir(fn);
#if 0
	if (!p)
		return nullptr;
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
#else
	return p;
#endif
}

const char* vpGetFileContainer(TFE_VPATH vpathid, const char *fn, bool matchcase)
{
	if (!fn)
		return nullptr;
	char *fp = to_vpath(vpathid, fn);
	if (!fp)
		return nullptr;

	const char *x = _vpGetFileContainer(fp, matchcase);
	PWDBG(stderr, "vpGetFileContainer(%s, %d) = %s\n", fp, matchcase, x);
	free(fp);
	return x;
}

/******************************************************************************/
/** vpFile **/

vpFile::vpFile()
{
	m_handle = nullptr;
	error = false;
	wmode = false;
}

vpFile::vpFile(TFE_VPATH vpathid, const char* name, char** buf, unsigned int* size, bool matchcase)
{
	m_handle = nullptr;
	error = false;
	if (!openread(vpathid, name, matchcase) || !readallocbuffer(buf, size))
		error = true;
}

vpFile::vpFile(TFEMount m, const char* name, char** buf, unsigned int* size, bool matchcase)
{
	m_handle = nullptr;
	error = false;
	if (!openread(m->vp, name, matchcase) || !readallocbuffer(buf, size))
		error = true;
}

vpFile::vpFile(TFE_VPATH vpathid, const char* name, bool matchcase)
{
	m_handle = nullptr;
	error = false;
	openread(vpathid, name, matchcase);
}

vpFile::vpFile(TFEMount mnt, const char* name, bool matchcase)
{
	m_handle = nullptr;
	error = false;
	openread(mnt->vp, name, matchcase);
}

vpFile::~vpFile()
{
	if (m_handle)
		vpClose(m_handle);
	m_handle = nullptr;
}

bool vpFile::openread(TFE_VPATH vpathid, const char* name, bool matchcase)
{
	if (m_handle)
		close();
	m_handle = (PHYSFS_File*)vpFileOpen(name, vpathid, matchcase);
	wmode = false;
	return m_handle != nullptr;
}

bool vpFile::openread(const char *filepath, bool matchcase)
{
	if (m_handle)
		close();
	m_handle = (PHYSFS_File*)vpFileOpen(filepath, VPATH_NONE, matchcase);
	wmode = false;
	return m_handle != nullptr;
}

// read. supply nullptr buffer to just skip over 'size' bytes.
int vpFile::read(void *buffer, unsigned int size)
{
	PHYSFS_sint64 r = -1;
	if (m_handle) {
		if (buffer) {
			r = PHYSFS_readBytes(m_handle, buffer, size);
		} else {
			PHYSFS_sint64 s = PHYSFS_tell(m_handle);
			if (s >= 0) {
				s += size;
				int ret = PHYSFS_seek(m_handle, s);
				if (ret != 0) {
					return size;
				} else {
					error = true;
					return -1;
				}
			}
			error = true;
			return -1;  // error seeking
		}
	} else {
		error = true;
	}
	return r;
}

void vpFile::close(void)
{
	if (m_handle)
		vpClose(m_handle);
	m_handle = nullptr;
}

PHYSFS_sint64 vpFile::size(void)
{
	PHYSFS_sint64 s = 0;
	if (m_handle)
		s = PHYSFS_fileLength(m_handle);
	if (s < 0) {	// can not determine
		error = true;
		s = 0;
	}
	return s;
}

PHYSFS_sint64 vpFile::tell(void)
{
	PHYSFS_sint64 s = 0;
	if (m_handle)
		s = PHYSFS_tell(m_handle);
	if (s < 0) {
		error = true;
		s = 0;
	}
	return s;
}

bool vpFile::seek(PHYSFS_uint64 pos)
{
	bool ret = false;
	if (m_handle)
		ret = (0 != PHYSFS_seek(m_handle, pos));
	return ret;
}

bool vpFile::eof(void)
{
	bool eof = m_handle ? PHYSFS_eof(m_handle) : true;
	return eof;
}

bool vpFile::openwrite(const char *fn, TFE_WMODE mode)
{
	if (m_handle)
		close();
	m_handle = vpOpenWrite(fn, mode);
	wmode = true;
	return m_handle != nullptr;
}

bool vpFile::write(const void *buffer, unsigned int size)
{
	if (!m_handle || !wmode || !buffer || size < 1)
		return false;
	PHYSFS_sint64 s = PHYSFS_writeBytes(m_handle, buffer, size);
	return s == size;
}

// read a whole file into memory, allocate a buffer for it too
bool vpFile::readallocbuffer(char **buf, unsigned int *size)
{
	if (!m_handle || !buf)
		return false;
	PHYSFS_sint64 s = this->size();
	char *tmpbuf = (char *)malloc(s);
	if (!tmpbuf)
		return false;
	int ret = this->read(tmpbuf, s);
	if (ret != s) {
		free(tmpbuf);
		return false;
	}
	if (size)
		*size = s;
	*buf = tmpbuf;
	return true;
}

/*
 * Stream reimplementation, BUT, assumes backing store is little endian
 * and does conversion accordingly.
 */
bool vpFile::read(uint8_t  *d) { return read(d, sizeof(uint8_t)); }
bool vpFile::read(uint16_t *d) { bool b = read(d, sizeof(uint16_t)); *d = SDL_SwapLE16(*d); return b; }
bool vpFile::read(uint32_t *d) { bool b = read(d, sizeof(uint32_t)); *d = SDL_SwapLE32(*d); return b; }
bool vpFile::read(uint64_t *d) { bool b = read(d, sizeof(uint64_t)); *d = SDL_SwapLE64(*d); return b; }
bool vpFile::read(int8_t   *d) { return read(d, sizeof(int8_t)); }
bool vpFile::read(int16_t  *d) { bool b = read(d, sizeof(int16_t)); *d = SDL_SwapLE16(*d); return b; }
bool vpFile::read(int32_t  *d) { bool b = read(d, sizeof(int32_t)); *d = SDL_SwapLE32(*d); return b; }
bool vpFile::read(int64_t  *d) { bool b = read(d, sizeof(int64_t)); *d = SDL_SwapLE64(*d); return b; }
bool vpFile::read(float    *d) { return read(d, sizeof(float)); }
bool vpFile::read(double   *d) { return read(d, sizeof(double)); }

bool vpFile::write(uint8_t  d) { return write(&d, sizeof(uint8_t)); }
bool vpFile::write(uint16_t d) { d = SDL_SwapLE16(d) ; return write(&d, sizeof(uint16_t)); }
bool vpFile::write(uint32_t d) { d = SDL_SwapLE32(d) ; return write(&d, sizeof(uint32_t)); }
bool vpFile::write(uint64_t d) { d = SDL_SwapLE64(d) ; return write(&d, sizeof(uint64_t)); }
bool vpFile::write(int8_t  d) { return write(&d, sizeof(int8_t)); }
bool vpFile::write(int16_t d) { d = SDL_SwapLE16(d) ; return write(&d, sizeof(int16_t)); }
bool vpFile::write(int32_t d) { d = SDL_SwapLE32(d) ; return write(&d, sizeof(int32_t)); }
bool vpFile::write(int64_t d) { d = SDL_SwapLE64(d) ; return write(&d, sizeof(int64_t)); }
bool vpFile::write(float   d) { return write(&d, sizeof(float)); }
bool vpFile::write(double  d) { return write(&d, sizeof(double)); }

bool vpFile::ok(void)
{
	return m_handle != nullptr;
}
