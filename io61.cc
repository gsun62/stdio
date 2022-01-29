#include "io61.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <climits>
#include <cerrno>
using namespace std;

// io61.c
//    YOUR CODE HERE!


// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.

struct io61_file {
    int fd;
    static constexpr off_t bufsize = 4096;
    unsigned char cbuf[bufsize];
    int mode; // keeps track of whether the file is read/write-only
    off_t tag; // file offset of first byte in cache (0 when file is opened)
    off_t end_tag; // file offset one past last valid byte in cache
    off_t pos_tag; // file offset of next char to read in cache
};


// io61_fdopen(fd, mode)
//    Return a new io61_file for file descriptor `fd`. `mode` is
//    either O_RDONLY for a read-only file or O_WRONLY for a
//    write-only file. You need not support read/write files.

io61_file* io61_fdopen(int fd, int mode) {
    assert(fd >= 0);
    io61_file* f = new io61_file;
    f->fd = fd;
    f->mode = mode; // initialize the mode of the file
    return f;
}


// io61_close(f)
//    Close the io61_file `f` and release all its resources.

int io61_close(io61_file* f) {
    io61_flush(f);
    int r = close(f->fd);
    delete f;
    return r;
}


// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file* f) {
    unsigned char buf[1];
    if (io61_read(f, buf, 1) == 1) { // successful read of 1 character
        return buf[0];
    } else {
        return EOF;
    }
}


// io61_fill(f)
//    Fill the read cache with new data, starting from file offset `end_tag`.
//    Only called for read caches.
//    Returns -1 for an unsuccessful fill.

int io61_fill(io61_file* f) {
    
    // Reset the cache to empty.
    f->tag = f->pos_tag = f->end_tag;
    // Read data.
    ssize_t n = read(f->fd, f->cbuf, f->bufsize);
    if (n >= 0) {
        f->end_tag += n; // moves the end tag to the end of the buffer
    }
    return n;
}


// io61_read(f, buf, sz)
//    Read up to `sz` characters from `f` into `buf`. Returns the number of
//    characters read on success; normally this is `sz`. Returns a short
//    count, which might be zero, if the file ended before `sz` characters
//    could be read. Returns -1 if an error occurred before any characters
//    were read.

ssize_t io61_read(io61_file* f, unsigned char* buf, size_t sz) {
    size_t pos = 0;

    while (pos < sz) {
        if (f->pos_tag == f->end_tag) { // we've reached the end of the buffer
            if (io61_fill(f) == -1) { // case for an error before characters are read
                return -1;
            } 
            if (f->pos_tag == f->end_tag) { 
                break;
            }
        }
        // taking the min value ensures that we correctly handle sizes greater than
        // and less than sz, since we want to copy the correct read data
        int cpy_sz = min((int)(f->end_tag - f->pos_tag), (int)(sz - pos));
        memcpy(&buf[pos], &f->cbuf[f->pos_tag - f->tag], cpy_sz); // copy our read data
        f->pos_tag += cpy_sz;
        pos += cpy_sz;
    }
    return pos;
}


// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file* f, int ch) {
    unsigned char buf[1];
    buf[0] = ch;
    if (io61_write(f, buf, 1) == 1) { // successful write of 1 character
        return 0;
    } else {
        return -1;
    }
}


// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file* f, const unsigned char* buf, size_t sz) {
    size_t pos = 0;

    while (pos < sz) {
        if (f->end_tag == f->tag + f->bufsize) { // we've reached the end of the buffer
            if (io61_flush(f) == -1) { // case for an error before characters are written
                return -1;
            }
        }
        // taking the min value ensures that we correctly handle sizes greater than
        // and less than sz, since we want to copy the correct write data
        int cpy_sz = min((int)(f->bufsize + f->tag - f->end_tag), (int)(sz - pos));
        memcpy(&f->cbuf[f->pos_tag - f->tag], &buf[pos], cpy_sz);
        f->pos_tag += cpy_sz;
        f->end_tag += cpy_sz; // same as pos_tag, since unwritten bytes are not valid
        pos += cpy_sz;
    }
    return pos;
}


// io61_flush(f)
//    Forces a write of all buffered data written to `f`.
//    If `f` was opened read-only, io61_flush(f) may either drop all
//    data buffered for reading, or do nothing.

int io61_flush(io61_file* f) {
    if (f->mode == O_RDONLY) { // check if f was opened read-only
        return 0;
    }
    // write data
    ssize_t r = write(f->fd, f->cbuf, f->pos_tag - f->tag);
    f->tag = f->pos_tag;
    return r; // returns -1 if there is a write error
}


// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file* f, off_t pos) {
    off_t align_pos = pos - pos % 4096; // aligns our seek with the buffer
    off_t r;

    // if the pos is already in the cache, optimize by avoiding system calls
    if (f->tag <= pos && pos < f->end_tag) {
        f->pos_tag = pos; // update cache tag
        return 0;
    }

    if (f->mode == O_RDONLY) { // seeks for read-only files
        r = lseek(f->fd, align_pos, SEEK_SET);
        if (r != align_pos) { // checks whether the lseek system call fails
            return -1;
        }
        f->end_tag = align_pos; // updating cache tags
        if (io61_fill(f) == -1) { // fills the cache so that we can read from it
            return -1;
        }
        f->pos_tag = pos; // updating cache tags
    } else if (f->mode == O_WRONLY){ // seeks for write-only files
        if (io61_flush(f) == -1) { // flushes the cache so that we can write into it
            return -1;
        }
        r = lseek(f->fd, (off_t) pos, SEEK_SET);
        if (r != (off_t) pos) { // checks whether the lseek system call fails
            return -1;
        }
        // updating cache tags
        f->tag = align_pos;
        f->pos_tag = pos;
        f->end_tag = pos;
    }
    return 0;
}


// You shouldn't need to change these functions.

// io61_open_check(filename, mode)
//    Open the file corresponding to `filename` and return its io61_file.
//    If `!filename`, returns either the standard input or the
//    standard output, depending on `mode`. Exits with an error message if
//    `filename != nullptr` and the named file cannot be opened.

io61_file* io61_open_check(const char* filename, int mode) {
    int fd;
    if (filename) {
        fd = open(filename, mode, 0666);
    } else if ((mode & O_ACCMODE) == O_RDONLY) {
        fd = STDIN_FILENO;
    } else {
        fd = STDOUT_FILENO;
    }
    if (fd < 0) {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(1);
    }
    return io61_fdopen(fd, mode & O_ACCMODE);
}


// io61_filesize(f)
//    Return the size of `f` in bytes. Returns -1 if `f` does not have a
//    well-defined size (for instance, if it is a pipe).

off_t io61_filesize(io61_file* f) {
    struct stat s;
    int r = fstat(f->fd, &s);
    if (r >= 0 && S_ISREG(s.st_mode)) {
        return s.st_size;
    } else {
        return -1;
    }
}