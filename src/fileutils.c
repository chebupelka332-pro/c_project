#include "fileutils.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <linux/limits.h> // PATH_MAX


int FileExists(const char *path)
{
    return access(path, F_OK) == 0;
}

int IsDirectory(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0) && S_ISDIR(st.st_mode);
}

uint64_t GetFileSize(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (!f)
        return -1;

    fseek(f, 0, SEEK_END);
    uint64_t size = (uint64_t)ftell(f);
    fclose(f);
    return size;
}

static void CollectFilesRecursively(const char *basePath, FileList *list)
{
    DIR *dir = opendir(basePath);
    if (!dir)
        return;

    struct dirent *entry;
    while ((entry = readdir(dir)))
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
            continue;

        char fullPath[PATH_MAX];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", basePath, entry->d_name);

        if (IsDirectory(fullPath))
        {
            CollectFilesRecursively(fullPath, list);
        }
        else
        {
            list->paths = realloc(list->paths, sizeof(char*) * (list->count + 1));
            list->paths[list->count] = malloc(sizeof(char) * (strlen(fullPath) + 1));
            strcpy(list->paths[list->count], fullPath);
            list->count++;
        }
    }

    closedir(dir);
}

FileList GetFilesInDirectory(const char *dirPath)
{
    FileList list = {NULL, 0};
    CollectFilesRecursively(dirPath, &list);
    return list;
}

void FreeFileList(FileList list)
{
    for (size_t i = 0; i < list.count; i++)
        free(list.paths[i]);

    free(list.paths);
}

unsigned char *ReadBinaryFile(const char *path, size_t *sizeOut)
{
    FILE *f = fopen(path, "rb");

    if (!f)
        return NULL;

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    unsigned char *buffer = malloc(size);
    if (!buffer)
    {
        fclose(f);
        return NULL;
    }

    if (fread(buffer, 1, size, f) != (size_t)size)
    {
        fclose(f);
        free(buffer);
        return NULL;
    }

    fclose(f);
    *sizeOut = size;
    return buffer;
}

int WriteBinaryFile(const char *path, const unsigned char *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return 0;

    int success = fwrite(data, 1, size, f) == size;
    fclose(f);
    return success;
}

int CreateDirectoryRecursive(const char *path)
{
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    // Проверка на NULL или пустой путь
    if (path == NULL || *path == '\0')
    {
        errno = EINVAL; // Неверный аргумент
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);

    if (len > 1 && tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    if (strcmp(tmp, ".") == 0)
    {
        struct stat st_check_dot;
        if (stat(tmp, &st_check_dot) == 0 && S_ISDIR(st_check_dot.st_mode))
            return 0;

        return -1;
    }

    char *start_scan = tmp;
    if (tmp[0] == '/')
        start_scan++;

    for (p = strchr(start_scan, '/'); p != NULL; p = strchr(p + 1, '/'))
    {
        *p = '\0';

        if (mkdir(tmp, 0755) != 0)
        {
            if (errno == EEXIST) // Путь уже существует
            {
                struct stat st_inter;
                if (stat(tmp, &st_inter) != 0)
                {
                    *p = '/';
                    return -1;
                }
                if (!S_ISDIR(st_inter.st_mode)) // Существует, но это НЕ директория
                {
                    *p = '/';
                    errno = ENOTDIR;
                    return -1;
                }
            }
            else
            {
                *p = '/';
                return -1;
            }
        }
        *p = '/';
    }
    if (mkdir(tmp, 0755) != 0)
    {
        if (errno == EEXIST) // Конечный путь уже существует
        {
            struct stat st_final;
            if (stat(tmp, &st_final) != 0)
                return -1;

            if (!S_ISDIR(st_final.st_mode))
            {
                errno = ENOTDIR;
                return -1;
            }
        }
        else // Другая ошибка при вызове mkdir для конечной директории
            return -1;
    }

    return 0;
}

const char *GetFileName(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}
