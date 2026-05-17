#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// From: https://www.gnu.org/software/tar/manual/html_node/Standard.html
struct posix_header {   /* byte offset */
    char name[100];     /*   0 */
    char mode[8];       /* 100 */
    char uid[8];        /* 108 */
    char gid[8];        /* 116 */
    char size[12];      /* 124 */
    char mtime[12];     /* 136 */
    char chksum[8];     /* 148 */
    char typeflag;      /* 156 */
    char linkname[100]; /* 157 */
    char magic[6];      /* 257 */
    char version[2];    /* 263 */
    char uname[32];     /* 265 */
    char gname[32];     /* 297 */
    char devmajor[8];   /* 329 */
    char devminor[8];   /* 337 */
    char prefix[155];   /* 345 */
    char padding[12];   /* 500 */
};

struct command_line_arguments {
    const char **bare_args;
    size_t bare_args_count;
    bool f_flag;
    bool t_flag;
    bool x_flag;
    bool v_flag;
};

void err_exit(const char *message, int exit_code) {
    fprintf(stderr, "mytar: %s\n", message);
    exit(exit_code);
}

struct command_line_arguments parse_command_line_arguments(int argc, char *argv[]) {
    struct command_line_arguments args = {0};
    // This memory lives until the end of the program, so we don't need to free it.
    args.bare_args = malloc(argc * sizeof(char *)); // worst case: all args are bare args
    args.bare_args_count = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (size_t j = 1; argv[i][j] != '\0'; j++) {
                switch (argv[i][j]) {
                case 'f':
                    args.f_flag = true;
                    break;
                case 't':
                    args.t_flag = true;
                    break;
                case 'x':
                    args.x_flag = true;
                    break;
                case 'v':
                    args.v_flag = true;
                    break;
                default:
                    fprintf(stderr, "mytar: invalid option -- '%c'\n", argv[i][j]);
                    err_exit("mytar: Error is not recoverable: exiting now", 2);
                }
            }
        } else {
            args.bare_args[args.bare_args_count++] = argv[i];
        }
    }
    if (!args.f_flag) {
        err_exit("mytar: option requires an argument -- 'f'", 2);
    }
    if (args.t_flag && args.x_flag) {
        err_exit("mytar: cannot specify both -t and -x options", 2);
    }
    return args;
}
uint64_t octal_or_base256_to_int(const char *str, size_t size) {
    uint64_t result = 0;
    if (str[0] & 0x80) {
        // base-256 encoding
        for (size_t i = 1; i < size; i++) {
            result <<= 8;
            result |= (uint64_t)(uint8_t)str[i];
        }
    } else {
        // octal encoding
        for (size_t i = 0; i < size && str[i] != '\0'; i++) {
            result <<= 3; // multiply by 8
            result += str[i] - '0';
        }
    }
    return result;
}

bool read_posix_header(FILE *archive, struct posix_header *header) {
    size_t bytes_read = fread(header, 1, sizeof(struct posix_header), archive);
    if (bytes_read < sizeof(struct posix_header)) {
        if (feof(archive)) {
            return false; // end of archive
        } else {
            fprintf(stderr, "mytar: Error reading archive\n");
        }
        err_exit("mytar: Error is not recoverable: exiting now", 2);
    }
    return true;
}

bool is_zero_block(const struct posix_header *header) {
    const char *ptr = (const char *)header;
    for (size_t i = 0; i < sizeof(struct posix_header); i++) {
        if (ptr[i] != '\0') {
            return false;
        }
    }
    return true;
}

void skip_bytes(FILE *archive, uint64_t bytes_to_skip) {
    if (fseek(archive, bytes_to_skip, SEEK_CUR) != 0) {
        fprintf(stderr, "mytar: Unexpected EOF in archive\n");
        err_exit("mytar: Error is not recoverable: exiting now", 2);
    }
}

void assert_valid_posix_header(const struct posix_header *header) {
    if (header->typeflag != '0' && header->typeflag != '\0') {
        fprintf(stderr, "mytar: Unsupported header type: %d\n", header->typeflag);
        exit(2);
    }
}
size_t contains(const char *str, const char **arr, size_t arr_size) {
    for (size_t i = 0; i < arr_size; i++) {
        if (strcmp(str, arr[i]) == 0) {
            return i;
        }
    }
    return -1; // not found
}

void list_archive_contents(const char *archive_file, const char **files_to_list, size_t files_to_list_count) {
    FILE *archive = fopen(archive_file, "rb");
    if (archive == NULL) {
        fprintf(stderr, "mytar: %s: Cannot open", archive_file);
        err_exit("Error is not recoverable: exiting now", 2);
    }
    bool *found_files = malloc(files_to_list_count * sizeof(bool)); // worst case: all files are found
    size_t zero_block_count = 0;
    size_t files = 0;
    for (size_t i = 0; i < files_to_list_count; i++) {
        found_files[i] = false;
    }
    while (true) {
        struct posix_header header;
        if (!read_posix_header(archive, &header)) {
            break; // end of archive
        }
        if (is_zero_block(&header)) {
            zero_block_count++;
            continue;
        }

        assert_valid_posix_header(&header);
        uint64_t file_size = octal_or_base256_to_int(header.size, sizeof(header.size));
        size_t found_files_index;
        if (files_to_list_count == 0) {
            printf("%s\n", header.name);
        } else if ((found_files_index = contains(header.name, files_to_list, files_to_list_count)) != (size_t)-1) {
            printf("%s\n", header.name);
            found_files[found_files_index] = true;
        }
        ++files;
        skip_bytes(archive, (file_size + 511) & ~511); // skip file content, round up to next 512-byte block
    }
    if (files_to_list_count > 0) {
        bool all_files_found = true;
        for (size_t i = 0; i < files_to_list_count; i++) {
            if (!found_files[i]) {
                fprintf(stderr, "mytar: %s: Not found in archive\n", files_to_list[i]);
                all_files_found = false;
            }
        }
        if (!all_files_found) {
            err_exit("Exiting with failure status due to previous errors", 2);
        }
    }

    if (zero_block_count == 1) {
        fprintf(stderr, "mytar: A lone zero block at %zu\n", files);
    }
    fclose(archive);
    free(found_files); // All other paths directly exit, so memory leak doesn't happen
}

// void extract_archive(const char *archive_file, bool verbose) {}
int main(int argc, char *argv[]) {
    if (argc < 2) {
        err_exit("mytar: need at least one option", 2);
    }

    struct command_line_arguments args = parse_command_line_arguments(argc, argv);
    if (args.x_flag) {
        // extract_archive(args.f_arg, args.v_option_present);
    } else if (args.t_flag) {
        if (args.bare_args_count == 0) {
            err_exit("mytar: you must specify at least one file to list with -t option", 64);
        }
        list_archive_contents(args.bare_args[0], args.bare_args + 1, args.bare_args_count - 1);
    } else {
        err_exit("you must specify either -t or -x option", 64);
    }
    return 0;
}