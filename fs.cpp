#include <iostream>
#include "fs.h"
#include <string>
#include <cstring>

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{
}

// formats the disk, i.e., creates an empty file system
int FS::format()
{
    std::cout << "FS::format()\n";

    // Initialize the FAT
    fat[0] = ROOT_BLOCK; // root directory
    fat[1] = FAT_BLOCK; // FAT
    for (int i = 2; i < disk.get_no_blocks(); i++)
    {
        fat[i] = FAT_FREE;
    }

    // Write FAT to disk
    if (disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat)) != 0) {
        std::cerr << "Error writing FAT to disk.\n";
        return -1; // or other appropriate error code
    }

    // Initialize root directory
    uint8_t rootDir[BLOCK_SIZE] = {0}; // Assuming you want it empty, adjust as needed
    if (disk.write(ROOT_BLOCK, rootDir) != 0) {
        std::cerr << "Error writing root directory to disk.\n";
        return -1; // or other appropriate error code
    }

    // Additional logic to reset or prepare the disk file, if needed
    // ...

    return 0;
}


// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filename) {
    std::cout << "FS::create(" << filename << ")\n";
    
    // 1. Find a free block
    int free_block = -1;
    for (int i = 2; i < disk.get_no_blocks(); ++i) { // start from 2 because 0 and 1 are reserved
        if (fat[i] == FAT_FREE) {
            free_block = i;
            break;
        }
    }

    // If no free blocks found, return error.
    if (free_block == -1) {
        std::cerr << "No free blocks available." << std::endl;
        return -1;
    }

    // 2. Check if file already exists
    uint8_t root_dir_data[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, root_dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry*>(root_dir_data);
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (std::string(dir_entries[i].file_name) == filename) {
            std::cerr << "File already exists." << std::endl;
            return -1;
        }
    }

    // 3. Create the file
    struct dir_entry new_entry;
    strncpy(new_entry.file_name, filename.c_str(), sizeof(new_entry.file_name));
    new_entry.first_blk = free_block;
    new_entry.size = 0; // will be updated as we write content
    new_entry.type = TYPE_FILE;
    new_entry.access_rights = READ | WRITE; // default rights

    // 4. Write content to file until an empty row is detected
    std::string input_line;
    uint8_t buffer[BLOCK_SIZE] = {0};
    int buffer_offset = 0;
    while (true) {
        std::getline(std::cin, input_line);
        if (input_line.empty()) break; // Stop if input is an empty row

        // If this line won't fit in the current buffer, write current buffer to disk
        if (buffer_offset + input_line.size() + 1 > BLOCK_SIZE) {
            disk.write(free_block, buffer);
            memset(buffer, 0, BLOCK_SIZE); // Clear buffer
            buffer_offset = 0;

            // 5. Handle the case where the file spans multiple blocks.
            // Find another free block
            int next_free_block = -1;
            for (int i = free_block + 1; i < disk.get_no_blocks(); ++i) {
                if (fat[i] == FAT_FREE) {
                    next_free_block = i;
                    break;
                }
            }
            if (next_free_block == -1) {
                std::cerr << "Out of disk space while writing file content." << std::endl;
                return -1;
            }

            // Update FAT
            fat[free_block] = next_free_block;
            free_block = next_free_block;
        }

        // Copy line to buffer and adjust offset
        memcpy(buffer + buffer_offset, input_line.c_str(), input_line.size());
        buffer_offset += input_line.size() + 1; // +1 for null terminator
        new_entry.size += input_line.size() + 1;
    }

    // Write the remaining buffer to disk
    if (buffer_offset > 0) {
        disk.write(free_block, buffer);
    }

    // Set the FAT entry for the last block to EOF
    fat[free_block] = FAT_EOF;

    // Update root directory with the new entry
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (dir_entries[i].file_name[0] == '\0') { // Check for empty slot
            dir_entries[i] = new_entry;
            break;
        }
    }

    // Write back the updated root directory and FAT to the disk
    disk.write(ROOT_BLOCK, root_dir_data);
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    return 0;
}



// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filename) {
    std::cout << "FS::cat(" << filename << ")\n";

    // 1. Check if the file exists in the root directory
    uint8_t root_dir_data[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, root_dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry*>(root_dir_data);
    int file_start_block = -1;
    uint32_t file_size = 0;
    
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (std::string(dir_entries[i].file_name) == filename) {
            file_start_block = dir_entries[i].first_blk;
            file_size = dir_entries[i].size;
            break;
        }
    }

    if (file_start_block == -1) {
        std::cerr << "File not found." << std::endl;
        return -1;
    }

    // 2. Fetch the starting block and read file contents
    uint8_t buffer[BLOCK_SIZE] = {0};
    int current_block = file_start_block;
    uint32_t bytes_read = 0;
    
    while (current_block != FAT_EOF) {
        disk.read(current_block, buffer);

        // 3. Print the block's contents. Keep track of bytes printed to not exceed file size.
        for (int i = 0; i < BLOCK_SIZE && bytes_read < file_size; ++i, ++bytes_read) {
            std::cout << buffer[i];
        }

        // 4. Move to the next block using FAT
        current_block = fat[current_block];
    }
    std::cout << std::endl; // Print a newline for formatting

    return 0;
}


// ls lists the content in the currect directory (files and sub-directories)
int FS::ls() {
    std::cout << "FS::ls()\n";

    // 1. Read the root directory block from disk.
    uint8_t root_dir_data[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, root_dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry*>(root_dir_data);

    std::cout << "name" << " " << "size" << std::endl; // Header

    // 2. Iterate over each directory entry.
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (dir_entries[i].file_name[0] != '\0') { // Check if the directory entry is valid.
            // 3. Print the name and size of the file.
            std::cout << dir_entries[i].file_name << " " << dir_entries[i].size << std::endl;
        }
    }

    return 0;
}


// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string sourcepath, std::string destpath) {
    std::cout << "FS::cp()\n";

    // 1. Find Source File
    uint8_t root_dir_data[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, root_dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry*>(root_dir_data);
    
    int sourceIndex = -1;
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(dir_entries[i].file_name, sourcepath.c_str()) == 0) {
            sourceIndex = i;
            break;
        }
    }

    if (sourceIndex == -1) {
        std::cerr << "Source file not found.\n";
        return -1;
    }

    dir_entry sourceFile = dir_entries[sourceIndex];

    // 2. Read Source File Content
    uint32_t sourceSize = sourceFile.size;
    uint8_t *sourceData = new uint8_t[sourceSize];
    int currentBlock = sourceFile.first_blk;
    int bytesRead = 0;
    while (bytesRead < sourceSize) {
        uint8_t tempData[BLOCK_SIZE];
        disk.read(currentBlock, tempData);

        for (int i = 0; (i < BLOCK_SIZE) && (bytesRead < sourceSize); ++i) {
            sourceData[bytesRead++] = tempData[i];
        }

        currentBlock = fat[currentBlock];
    }

    // 3. Locate Free Directory Entry
    int destIndex = -1;
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (dir_entries[i].file_name[0] == '\0') {
            destIndex = i;
            break;
        }
    }

    if (destIndex == -1) {
        std::cerr << "Directory full. Cannot copy file.\n";
        delete[] sourceData;
        return -1;
    }

    // 4. Write Destination File Content
    int destFirstBlock = -1;
    int currentFreeBlock = FAT_FREE;
    for (int i = 0; i < (BLOCK_SIZE / 2); ++i) {
        if (fat[i] == FAT_FREE) {
            currentFreeBlock = i;
            if (destFirstBlock == -1) {
                destFirstBlock = i;
            }
            fat[i] = FAT_EOF; // Assuming we will fill this block. Adjusted later if not.

            // Write data to this block
            uint8_t tempData[BLOCK_SIZE];
            for (int j = 0; (j < BLOCK_SIZE) && (bytesRead < sourceSize); ++j) {
                tempData[j] = sourceData[bytesRead++];
            }
            disk.write(currentFreeBlock, tempData);

            if (bytesRead == sourceSize) {
                break;
            }
        }
    }

    // Update directory entry for the destination
    dir_entries[destIndex] = sourceFile; // Copy all properties of the source file
    strncpy(dir_entries[destIndex].file_name, destpath.c_str(), sizeof(dir_entries[destIndex].file_name) - 1);
    dir_entries[destIndex].file_name[sizeof(dir_entries[destIndex].file_name) - 1] = '\0'; // Null terminate
    dir_entries[destIndex].first_blk = destFirstBlock;

    // 5. Update Directory and FAT
    disk.write(ROOT_BLOCK, root_dir_data);
    
    uint8_t fat_data[BLOCK_SIZE];
    memcpy(fat_data, fat, BLOCK_SIZE);
    disk.write(FAT_BLOCK, fat_data);

    delete[] sourceData;

    return 0;
}


// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath) {
    std::cout << "FS::mv()\n";

    // 1. Locate the source file
    uint8_t root_dir_data[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, root_dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry*>(root_dir_data);
    
    int sourceIndex = -1;
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(dir_entries[i].file_name, sourcepath.c_str()) == 0) {
            sourceIndex = i;
            break;
        }
    }

    if (sourceIndex == -1) {
        std::cerr << "Source file not found.\n";
        return -1;
    }

    // Check if the destination filename already exists
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(dir_entries[i].file_name, destpath.c_str()) == 0) {
            std::cerr << "Destination file name already exists.\n";
            return -1;
        }
    }

    // 2. Update its name to the desired destination name
    strncpy(dir_entries[sourceIndex].file_name, destpath.c_str(), sizeof(dir_entries[sourceIndex].file_name) - 1);
    dir_entries[sourceIndex].file_name[sizeof(dir_entries[sourceIndex].file_name) - 1] = '\0'; // Ensure null-termination

    // 3. Write back the modified directory entry to the disk
    disk.write(ROOT_BLOCK, root_dir_data);

    return 0;
}


// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath) {
    std::cout << "FS::rm()\n";

    // 1. Locate the file in the directory
    uint8_t root_dir_data[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, root_dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry*>(root_dir_data);
    
    int fileIndex = -1;
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(dir_entries[i].file_name, filepath.c_str()) == 0) {
            fileIndex = i;
            break;
        }
    }

    if (fileIndex == -1) {
        std::cerr << "File not found.\n";
        return -1;
    }

    // 2. Mark the file's blocks as free in the FAT
    int16_t currentBlock = dir_entries[fileIndex].first_blk;
    while (currentBlock != FAT_EOF) {
        disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
        int16_t nextBlock = fat[currentBlock];
        fat[currentBlock] = FAT_FREE;
        currentBlock = nextBlock;
        disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
    }

    // 3. Remove its directory entry and 4. Mark the directory entry space as free
    memset(&dir_entries[fileIndex], 0, sizeof(struct dir_entry));

    // Write back the modified directory to the disk
    disk.write(ROOT_BLOCK, root_dir_data);

    return 0;
}


// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filename1, std::string filename2) {
    std::cout << "FS::append()\n";

    // 1. Locate both files in the directory
    uint8_t root_dir_data[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, root_dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry*>(root_dir_data);
    
    int srcIndex = -1, destIndex = -1;
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(dir_entries[i].file_name, filename1.c_str()) == 0) {
            srcIndex = i;
        }
        if (strcmp(dir_entries[i].file_name, filename2.c_str()) == 0) {
            destIndex = i;
        }
    }

    if (srcIndex == -1 || destIndex == -1) {
        std::cerr << "One or both files not found.\n";
        return -1;
    }

    // Navigate to the end of filename2 using FAT
    int16_t currentBlockDest = dir_entries[destIndex].first_blk;
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
    while (fat[currentBlockDest] != FAT_EOF) {
        currentBlockDest = fat[currentBlockDest];
    }

    // 2. & 3. Read blocks of filename1 and append them to filename2
    int16_t currentBlockSrc = dir_entries[srcIndex].first_blk;
    while (currentBlockSrc != FAT_EOF) {
        uint8_t block_data[BLOCK_SIZE];

        // Read block from source file
        disk.read(currentBlockSrc, block_data);

        // Find a free block for the destination file
        int16_t freeBlockDest = -1;
        for (int i = 0; i < (BLOCK_SIZE / 2); ++i) {
            if (fat[i] == FAT_FREE) {
                freeBlockDest = i;
                break;
            }
        }

        if (freeBlockDest == -1) {
            std::cerr << "No free blocks left on disk.\n";
            return -1;
        }

        // Update FAT
        fat[currentBlockDest] = freeBlockDest;
        fat[freeBlockDest] = FAT_EOF;
        disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

        // Write the block data to the new location in filename2
        disk.write(freeBlockDest, block_data);

        // Move to the next block in source file
        currentBlockSrc = fat[currentBlockSrc];
        currentBlockDest = freeBlockDest;
    }

    // Update the size of filename2 in the directory
    dir_entries[destIndex].size += dir_entries[srcIndex].size;
    disk.write(ROOT_BLOCK, root_dir_data);

    return 0;
}


// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";
    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}