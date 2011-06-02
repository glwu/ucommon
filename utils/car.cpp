// Copyright (C) 2010 David Sugar, Tycho Softworks.
//
// This file is part of GNU uCommon C++.
//
// GNU uCommon C++ is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published
// by the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// GNU uCommon C++ is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with GNU uCommon C++.  If not, see <http://www.gnu.org/licenses/>.

#include <ucommon/secure.h>
#include <sys/stat.h>

using namespace UCOMMON_NAMESPACE;

static shell::flagopt helpflag('h',"--help",    _TEXT("display this list"));
static shell::flagopt althelp('?', NULL, NULL);
static shell::stringopt tag('t', "--tag", _TEXT("tag annotation"), "text", "");
static shell::stringopt algo('c', "--cipher", _TEXT("cipher method (aes256)"), "method", "aes256");
static shell::flagopt decode('d', "--decode", _TEXT("decode archive"));
static shell::stringopt hash('h', "--digest", _TEXT("digest method (sha)"), "method", "sha");
static shell::flagopt noheader('n', "--no-header", _TEXT("without wrapper"));
static shell::stringopt out('o', "--output", _TEXT("output file"), "filename", "-");
static shell::flagopt recursive('R', "--recursive", _TEXT("recursive directory scan"));
static shell::flagopt altrecursive('r', NULL, NULL);
static shell::flagopt hidden('s', "--hidden", _TEXT("include hidden files"));

static bool binary = false;
static int exit_code = 0;
static const char *argv0 = "car";
static unsigned char frame[48], cbuf[48];
static cipher_t cipher;
static FILE *output = stdout;
static enum {d_text, d_file, d_scan, d_init} decoder = d_init;
static unsigned frames;

static void report(const char *path, int code)
{
    const char *err = _TEXT("i/o error");

    switch(code) {
    case EACCES:
    case EPERM:
        err = _TEXT("permission denied");
        break;
    case EROFS:
        err = _TEXT("read-only file system");
        break;
    case ENODEV:
    case ENOENT:
        err = _TEXT("no such file or directory");
        break;
    case ENOTDIR:
        err = _TEXT("not a directory");
        break;
    case ENOTEMPTY:
        err = _TEXT("directory not empty");
        break;
    case ENOSPC:
        err = _TEXT("no space left on device");
        break;
    case EBADF:
    case ENAMETOOLONG:
        err = _TEXT("bad file path");
        break;
    case EBUSY:
    case EINPROGRESS:
        err = _TEXT("file or directory busy");
        break;
    case EINTR:
        err = _TEXT("operation interupted");
        break;
    case EISDIR:
        err = _TEXT("is a directory");
        break;
#ifdef  ELOOP
    case ELOOP:
        err = _TEXT("too many sym links");
        break;
#endif
    }

    if(path)
        shell::printf("%s: %s: %s\n", argv0, path, err);
    else
        shell::errexit(1, "*** %s: %s\n", argv0, err);

    exit_code = 1;
}

static bool encode(const char *path, FILE *fp, size_t offset = 0)
{
    memset(frame, 0, sizeof(frame));

    size_t count = fread(frame + offset, 1, sizeof(frame) - offset, fp);
    char buffer[128];

    if(ferror(fp)) {
        report(path, errno);
        return false;
    }

    if(count)
        count += offset;

    // add padd value for last frame...
    if(count < sizeof(frame))
        frame[sizeof(frame) - 1] = (char)(count - offset);

    // cipher it...
    size_t encoded = cipher.put(frame, sizeof(frame));
    if(encoded != sizeof(frame)) {
        report(path, EINTR);
        return false;
    }

    if(binary)
        fwrite(cbuf, sizeof(cbuf), 1, output);
    else {
        String::b64encode(buffer, cbuf, 48);
        fprintf(output, "%s\n", buffer);
    }

    // return status
    if(count == sizeof(frame))
        return true;

    return false;
}

static void encodestream(void)
{

    binary = false;
    size_t offset = 6;

    if(fsys::istty(shell::input()))
        fputs("car: type your message\n", stderr);

    memset(frame, 0, sizeof(frame));

    for(;;) {
        if(!encode("-", stdin, offset))
            break;
        offset = 0;
    }

    if(!binary && !is(noheader))
        fprintf(output, "-----END CAR STREAM-----\n");
}

static void header(void)
{
    binary = true;

    memset(frame, 0, sizeof(frame));
    String::set((char *)frame, 6, ".car");
    frame[5] = 1;
    frame[4] = 0xff;
    String::set((char *)frame + 6, sizeof(frame) - 6, *tag);
    fwrite(frame, sizeof(frame), 1, output);
}

static void encodefile(const char *path, const char *name)
{
    char buffer[128];

    fsys::fileinfo_t ino;

    fsys::fileinfo(path, &ino);

    FILE *fp = fopen(path, "r");
    if(!fp) {
        report(name, errno);
        return;
    }

    lsb_setlong(frame, ino.st_size);
    frame[4] = 1;
    frame[5] = 0;
    String::set((char *)(frame + 6), sizeof(frame) - 6, name);

    cipher.put(frame, sizeof(frame));

    if(binary)
        fwrite(cbuf, sizeof(cbuf), 1, output);
    else {
        String::b64encode(buffer, cbuf, 48);
        fprintf(output, "%s\n", buffer);
    }

    for(;;) {
        if(!encode(name, fp))
            break;
    }
}

static void final(void)
{
    size_t size;

    switch(decoder) {
    case d_init:
        return;
    case d_scan:
        cipher.put(frame, sizeof(frame));
        if(cbuf[4] > 0)
            return;
        size = cbuf[sizeof(frame) - 1];
        if(size)
            fwrite(cbuf + 6, size, 1, output);
        break;
    default:
        decoder = d_scan;
        cipher.put(frame, sizeof(frame));
        size = cbuf[sizeof(frame) - 1];
        if(size)
            fwrite(cbuf, size, 1, output);
    }
}

static void process(void)
{
    string_t path;
    char *cp;
    switch(decoder) {
    case d_init:
        decoder = d_scan;
        return;
    case d_scan:
        cipher.put(frame, sizeof(frame));
        if(cbuf[4] == 0xff) // header...
            break;
        if(!cbuf[4]) {      // if message
            fwrite(cbuf + 6, sizeof(frame) - 6, 1, output);
            decoder = d_text;
            break;
        }
        decoder = d_file;
        frames = lsb_getlong(cbuf) / sizeof(frame);
        path = str((char *)(cbuf + 6));
        cp = strrchr((char *)(cbuf + 6), '/');
        if(cp) {
            *cp = 0;
            fsys::createDir((char *)(cbuf + 6), 0640);
        }
        output = fopen(*path, "w");
        if(!output)
            shell::errexit(8, "*** %s: %s: %s\n",
                argv0, *path, _TEXT("cannot create"));
        printf("decoding %s...\n", *path);
        break;
    case d_file:
        if(!frames) {
            final();
            break;
        }
        --frames;
    default:
        cipher.put(frame, sizeof(frame));
        fwrite(cbuf, sizeof(frame), 1, output);
    }
}

static void streamdecode(FILE *fp, const char *path)
{
    char buffer[128];
    for(;;) {
        fgets(buffer, sizeof(buffer), fp);
        if(feof(fp)) {
            shell::errexit(5, "*** %s: %s: %s\n",
                argv0, path, _TEXT("no archive found"));
        }
        if(ferror(fp)) {
            exit_code = errno;
            report(path, exit_code);
            return;
        }
        if(eq("-----BEGIN CAR STREAM-----\n", buffer))
            break;
    }
    for(;;) {
        if(feof(fp)) {
            final();
            return;
        }

        if(ferror(fp)) {
            exit_code = errno;
            report(path, exit_code);
            return;
        }

        fgets(buffer, sizeof(buffer), fp);
        if(eq("-----END CAR STREAM-----\n", buffer)) {
            final();
            return;
        }

        // ignore extra headers...
        if(strstr(buffer, ": "))
            continue;

        process();
        unsigned len = String::b64decode(frame, buffer, 48);
        if(len < 48) {
            report(path, EINTR);
            return;
        }
    }
}

static void scan(string_t path, string_t prefix)
{
    char filename[128];
    string_t filepath;
    string_t name;
    string_t subdir;
    fsys_t dir(path, fsys::ACCESS_DIRECTORY);

    while(is(dir) && fsys::read(dir, filename, sizeof(filename))) {
        if(*filename == '.' && (filename[1] == '.' || !filename[1]))
            continue;

        if(*filename == '.' && !is(hidden))
            continue;

        filepath = str(path) + str("/") + str(filename);
        if(prefix[0])
            name ^= prefix + str("/") + str(filename);
        else
            name ^= str(filename);

        if(fsys::isdir(filepath)) {
            if(is(recursive) || is(altrecursive))
                scan(filepath, name);
            else
                report(*filepath, EISDIR);
        }
        else
            encodefile(*filepath, *name);
    }
}

PROGRAM_MAIN(argc, argv)
{
    shell::bind("car");
    shell args(argc, argv);
    argv0 = args.argv0();
    unsigned count = 0;
    char passphrase[256];
    char confirm[256];
    const char *ext;

    argv0 = args.argv0();

    if(is(helpflag) || is(althelp)) {
        printf("%s\n", _TEXT("Usage: car [options] path..."));
        printf("%s\n\n", _TEXT("Crytographic archiver"));
        printf("%s\n", _TEXT("Options:"));
        shell::help();
        printf("\n%s\n", _TEXT("Report bugs to dyfet@gnu.org"));
        PROGRAM_EXIT(0);
    }

    if(!secure::init())
        shell::errexit(1, "*** %s: %s\n", argv0, _TEXT("not supported"));

    if(!Digest::is(*hash))
        shell::errexit(2, "*** %s: %s: %s\n",
            argv0, *hash, _TEXT("unkown or unsupported digest method"));

    if(!Cipher::is(*algo))
        shell::errexit(2, "*** %s: %s: %s\n",
            argv0, *algo, _TEXT("unknown or unsupported cipher method"));

    shell::getpass("passphrase: ", passphrase, sizeof(passphrase));
    shell::getpass("confirm: ", confirm, sizeof(confirm));

    if(!eq(passphrase, confirm))
        shell::errexit(3, "*** %s: %s\n",
            argv0, _TEXT("passphrase does not match confirmation"));

    // set key and cleanup buffers...
    skey_t key(*algo, *hash, passphrase);
    memset(passphrase, 0, sizeof(passphrase));
    memset(confirm, 0, sizeof(confirm));
    memset(cbuf, 0, sizeof(cbuf));

    if(is(decode))
        cipher.set(&key, Cipher::DECRYPT, cbuf, sizeof(cbuf));
    else
        cipher.set(&key, Cipher::ENCRYPT, cbuf, sizeof(cbuf));

    if(is(decode) && !args()) {
        streamdecode(stdin, "-");
        goto end;
    }

//  if(is(decode))
//      ;;

    if(!eq(*out, "-")) {
        output = fopen(*out, "w");
        if(!output) {
            shell::errexit(4, "*** %s: %s: %s\n",
                argv0, *out, _TEXT("cannot create"));
        }
    }

    // if we are outputting to a car file, do it in binary
    ext = strrchr(*out, '.');
    if(case_eq(ext, ".car"))
        header();

    if(!binary && !is(noheader)) {
        fprintf(output, "-----BEGIN CAR STREAM-----\n");
        if(tag)
            fprintf(output, "Tag: %s\n", *tag);
    }

    if(!args()) {
        encodestream();
        goto end;
    }

    while(count < args()) {
        if(fsys::isdir(args[count]))
            scan(str(args[count++]), "");
        else {
            const char *cp = args[count++];
            const char *ep = strrchr(cp, '/');
            if(!ep)
                ep = strrchr(cp, '\\');
            if(ep)
                ++ep;
            else
                ep = cp;
            encodefile(cp, ep);
        }
    }

    if(!binary && !is(noheader))
        fprintf(output, "-----END CAR STREAM-----\n");

end:
    PROGRAM_EXIT(exit_code);
}
