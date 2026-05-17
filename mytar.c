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
    const char *f_arg;
    const char **t_args;
    size_t t_args_count;
    bool x_options_present;
    bool v_option_present;
};
void err_exit(const char *message, int exit_code) {
    fprintf(stderr, "mytar: %s\n", message);
    exit(exit_code);
}

struct command_line_arguments parse_command_line_arguments(int argc, char *argv[]) {
    struct command_line_arguments result = {0};
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == 'f') {
                if (argv[i][2] != '\0') {
                    err_exit("option -f must be followed by a space and the archive file name", 64);
                }
                if (result.f_arg != NULL) {
                    err_exit("option -f specified multiple times", 64);
                }
                if (i + 1 < argc) {
                    result.f_arg = argv[i + 1];
                    i++;
                } else {
                    err_exit("option requires an argument -- 'f'", 64);
                }
            } else if (argv[i][1] == 't') {
                if (argv[i][2] != '\0') {
                    err_exit("option -t must be followed by a space and optionally files to list", 64);
                }
                size_t remaining_args = 0;
                for (int j = i + 1; j < argc; j++) {
                    if (argv[j][0] == '-') {
                        break;
                    }
                    remaining_args++;
                }
                result.t_args = (const char **)(&argv[i + 1]);
                result.t_args_count = remaining_args;
                i += remaining_args;
            } else if (argv[i][1] == 'x') {
                if (argv[i][2] != '\0') {
                    err_exit("option -x must be followed by a space", 64);
                }
                result.x_options_present = true;
            } else if (argv[i][1] == 'v') {
                if (argv[i][2] != '\0') {
                    err_exit("option -v must be followed by a space", 64);
                }
                result.v_option_present = true;
            } else {
                fprintf(stderr, "mytar: invalid option -- '%c'\n", argv[i][1]);
                exit(64);
            }
        }
    }
    if (result.f_arg == NULL) {
        err_exit("you must specify the archive file with -f", 64);
    }
    return result;
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
    size_t bytes_read = fread(&header, 1, sizeof(struct posix_header), archive);
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
        fprintf(stderr, "mytar: Unsupported header type: '%d'\n", header->typeflag);
    }
}
bool contains(const char *str, const char ** arr, size_t arr_size) {
    for (size_t i = 0; i < arr_size; i++) {
        if (strcmp(str, arr[i]) == 0) {
            return true;
        }
    }
    return false;
}

void list_archive_contents(const char *archive_file, const char **files_to_list, size_t files_to_list_count) {
    FILE *archive = fopen(archive_file, "rb");
    if (archive == NULL) {
        fprintf(stderr, "mytar: %s: Cannot open", archive_file);
        err_exit("Error is not recoverable: exiting now", 2);
    }
    char** found_files = malloc(files_to_list_count * sizeof(char*));
    size_t found_files_count = 0;
    size_t zero_block_count = 0;
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
        if (files_to_list_count == 0) {
            printf("%s\n", header.name);
        } else if (contains(header.name, files_to_list, files_to_list_count)) {
            printf("%s\n", header.name);
            found_files[found_files_count++] = header.name;
        }
        skip_bytes(archive, (file_size + 511) & ~511); // skip file content, round up to next 512-byte block
    }
    if (files_to_list_count > 0) {
        bool all_files_found = true;
        for (size_t i = 0; i < files_to_list_count; i++) {
            if (!contains(files_to_list[i], (const char **)found_files, found_files_count)) {
                fprintf(stderr, "mytar: %s: Not found in archive\n", files_to_list[i]);
                all_files_found = false;
            }
        }
        if (!all_files_found) {
            err_exit("Exiting with failure status due to previous errors", 2);
        }
    }

    if (zero_block_count == 1) {
        fprintf(stderr, "mytar: A lone zero block at 4\n");
    }
    fclose(archive);
}

// void extract_archive(const char *archive_file, bool verbose) {}
int main(int argc, char *argv[]) {
    if (argc < 2) {
        err_exit("mytar: need at least one option", 2);
    }

    struct command_line_arguments args = parse_command_line_arguments(argc, argv);
    if (args.x_options_present) {
        // extract_archive(args.f_arg, args.v_option_present);
    } else if (args.t_args_count > 0 || args.t_args != NULL) {
        list_archive_contents(args.f_arg, args.t_args, args.t_args_count);
    } else {
        err_exit("you must specify either -t or -x option", 64);
    }
    return 0;
}