#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <stddef.h>
#include <stdint.h>

// Тип для хранения списка файлов
typedef struct 
{
    char **paths;
    size_t count;
} FileList;

// Проверяет, существует ли файл
int FileExists(const char *path);

// Проверяет, является ли путь директорией
int IsDirectory(const char *path);

// Получает размер файла в байтах
uint64_t GetFileSize(const char *path);

// Рекурсивно собирает список всех файлов внутри папки
FileList GetFilesInDirectory(const char *dirPath);

// Освобождает память, выделенную для списка файлов
void FreeFileList(FileList list);

// Загружает содержимое файла в буфер
unsigned char *ReadBinaryFile(const char *path, size_t *sizeOut);

// Записывает буфер в бинарный файл
int WriteBinaryFile(const char *path, const unsigned char *data, size_t size);

// Создаёт директорию, включая родительские, если нужно
int CreateDirectoryRecursive(const char *path);

// Получает имя файла из полного пути (без папки)
const char *GetFileName(const char *path);

#endif
