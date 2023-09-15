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




int FS::cat(std::string filename) {
    std::cout << "FS::cat(" << filename << ")\n";

    uint8_t root_dir_data[BLOCK_SIZE];
    disk.read(ROOT_BLOCK, root_dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry*>(root_dir_data);

    int fileIndex = -1;
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(dir_entries[i].file_name, filename.c_str()) == 0) {
            fileIndex = i;
            break;
        }
    }

    if (fileIndex == -1) {
        std::cerr << "File not found.\n";
        return 0;
    }

    // Read the FAT
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    int16_t currentBlock = dir_entries[fileIndex].first_blk;
    while (currentBlock != FAT_EOF) {
        uint8_t block_data[BLOCK_SIZE];
        disk.read(currentBlock, block_data);

        // Print the content of the block as C-strings
        char *ptr = (char *)block_data;
        while (*ptr) { // Loop until we hit a null character
            std::cout << ptr << std::endl; // Print the string
            ptr += strlen(ptr) + 1; // Move to the next string in the block
        }

        currentBlock = fat[currentBlock];
    }
    return 0;
}
// ls lists the content in the current directory (files and sub-directories)
int FS::ls() {
    std::cout << "FS::ls()\n";

    // 1. Read the current directory from disk
    uint8_t current_dir_data[BLOCK_SIZE];
    disk.read(current_directory_block, current_dir_data);  // Changed from ROOT_BLOCK
    struct dir_entry *current_dir_entries = reinterpret_cast<struct dir_entry*>(current_dir_data);

    // 2. Iterate over all entries and print details
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        struct dir_entry *entry = &current_dir_entries[i];
        if (entry->file_name[0] != '\0') {  // valid entry
            std::cout << entry->file_name << " ";
            if (entry->type == TYPE_DIR) {
                std::cout << "dir ";
            } else {
                std::cout << "file ";
            }
            if (entry->size == 0 || entry->type == TYPE_DIR) {
                std::cout << "-\n";
            } else {
                std::cout << entry->size << "\n";
            }
        }
    }
    return 0;
}




// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>


int FS::cp(std::string sourcepath, std::string destpath) {
    std::cout << "FS::cp()\n";

    // 1. Find Source File in the current directory
    uint8_t current_dir_data[BLOCK_SIZE];
    disk.read(current_directory_block, current_dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry*>(current_dir_data);
    int destIndex = find_directory_entry(destpath, dir_entries);
    if (destIndex != -1) {
        if (dir_entries[destIndex].type == TYPE_DIR) {
            // Destination is a directory. Copy the source file into this directory.
            // You might need to adjust the path and handle it accordingly.
            destpath = destpath + "/" + sourcepath; // Adjusting the path
        } else {
            std::cerr << "Destination file name already exists.\n";
            return -1;
        }
    }
    int sourceIndex = find_directory_entry(sourcepath, dir_entries);
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
        // After reading the source file content
    std::cout << "Source file content: ";
    for (int i = 0; i < sourceSize; i++) {
        std::cout << sourceData[i];
    }
    std::cout << std::endl;


    // 3. Locate Free Directory Entry for Destination
    int destIndex = find_free_directory_entry(dir_entries);
    if (destIndex == -1) {
        std::cerr << "Directory full. Cannot copy file.\n";
        delete[] sourceData;
        return -1;
    }

    // 4. Write Destination File Content
    int destFirstBlock = find_free_fat_entry();
    if (destFirstBlock == -1) {
        std::cerr << "No free blocks. Cannot copy file.\n";
        delete[] sourceData;
        return -1;
    }
    
    int currentFreeBlock = destFirstBlock;
    bytesRead = 0; // reset bytesRead for writing purposes
    while (bytesRead < sourceSize) {
        if (fat[currentFreeBlock] == FAT_FREE) {
            fat[currentFreeBlock] = FAT_EOF;

            // Write data to this block
            uint8_t tempData[BLOCK_SIZE];
            for (int j = 0; (j < BLOCK_SIZE) && (bytesRead < sourceSize); ++j) {
                tempData[j] = sourceData[bytesRead++];
            }
            disk.write(currentFreeBlock, tempData);

            if (bytesRead == sourceSize) {
                break;
            }

            // Find next free block in FAT for next iteration
            currentFreeBlock = find_free_fat_entry(currentFreeBlock + 1);
            if (currentFreeBlock == -1) {
                std::cerr << "No more free blocks during copy.\n";
                delete[] sourceData;
                return -1;
            }
        }
    }

    // Update directory entry for the destination
// Update directory entry for the destination
    dir_entries[destIndex] = sourceFile;
    strncpy(dir_entries[destIndex].file_name, destpath.c_str(), sizeof(dir_entries[destIndex].file_name) - 1);
    dir_entries[destIndex].first_blk = destFirstBlock;
    dir_entries[destIndex].size = sourceSize;


    // 5. Update Directory and FAT
    disk.write(current_directory_block, current_dir_data);

    // Assuming your FAT is just a single block. 
    // If it's more than one block, this needs adjustments.
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
    // After finding the source file:
    int destIndex = find_directory_entry(destpath, dir_entries);
    if (destIndex != -1) {
        if (dir_entries[destIndex].type == TYPE_DIR) {
            // Destination is a directory. Move the source file into this directory.
            // You might need to adjust the path and handle it accordingly.
            destpath = destpath + "/" + sourcepath; // Adjusting the path
        } else {
            std::cerr << "Destination file name already exists.\n";
            return -1;
        }
    }

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

    // 1. Locate the file/directory in the current directory
    uint8_t current_dir_data[BLOCK_SIZE];
    disk.read(current_directory_block, current_dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry*>(current_dir_data);
    
    int entryIndex = -1;
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(dir_entries[i].file_name, filepath.c_str()) == 0) {
            entryIndex = i;
            break;
        }
    }

    if (entryIndex == -1) {
        std::cerr << "Entry not found.\n";
        return -1;
    }

    struct dir_entry target = dir_entries[entryIndex];

    // If the target is a directory, ensure it's empty
    if (target.type == TYPE_DIR) {
        uint8_t dir_data[BLOCK_SIZE];
        disk.read(target.first_blk, dir_data);
        struct dir_entry *entries = reinterpret_cast<struct dir_entry*>(dir_data);
        for (int i = 1; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) { // Start from 1 to skip ".." entry
            if (entries[i].file_name[0] != '\0') {
                std::cerr << "Error: Directory is not empty.\n";
                return -1;
            }
        }

        // If directory is empty, mark its block as free in the FAT
        disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
        fat[target.first_blk] = FAT_FREE;
        disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
    } 
    // If it's a file, mark its blocks as free in the FAT
    else {
        int16_t currentBlock = target.first_blk;
        while (currentBlock != FAT_EOF) {
            disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
            int16_t nextBlock = fat[currentBlock];
            fat[currentBlock] = FAT_FREE;
            currentBlock = nextBlock;
            disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
        }
    }

    // Remove its directory entry 
    memset(&dir_entries[entryIndex], 0, sizeof(struct dir_entry));

    // Write back the modified directory to the disk
    disk.write(current_directory_block, current_dir_data);

    return 0;
}



// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filename1, std::string filename2) {
    std::cout << "Executing FS::append()...\n";

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
        std::cerr << "Error: One or both files not found.\n";
        return -1;
    }

    std::cout << "Found " << filename1 << " with size: " << dir_entries[srcIndex].size << " bytes.\n";
    std::cout << "Found " << filename2 << " with size: " << dir_entries[destIndex].size << " bytes.\n";

    // Read the FAT
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    int16_t currentBlockDest = dir_entries[destIndex].first_blk;
    while (fat[currentBlockDest] != FAT_EOF) {
        currentBlockDest = fat[currentBlockDest];
    }

    uint8_t lastBlockData[BLOCK_SIZE];
    disk.read(currentBlockDest, lastBlockData);

    int positionInLastBlock = dir_entries[destIndex].size % BLOCK_SIZE;

    // Add a newline to the destination file
    if (positionInLastBlock < BLOCK_SIZE - 1) {
        lastBlockData[positionInLastBlock] = '\n';
        disk.write(currentBlockDest, lastBlockData);
    } else {
        int16_t newBlockForNewline = -1;
        for (int i = 1; i < (BLOCK_SIZE / 2); ++i) {
            if (fat[i] == FAT_FREE) {
                newBlockForNewline = i;
                break;
            }
        }
        if (newBlockForNewline == -1) {
            std::cerr << "Error: No free blocks left on disk for newline.\n";
            return -1;
        }
        fat[currentBlockDest] = newBlockForNewline;
        fat[newBlockForNewline] = FAT_EOF;
        memset(lastBlockData, 0, BLOCK_SIZE);
        lastBlockData[0] = '\n';
        disk.write(newBlockForNewline, lastBlockData);
        currentBlockDest = newBlockForNewline;
        dir_entries[destIndex].size += 1;
    }

    // Append the content of source to destination
    int16_t currentBlockSrc = dir_entries[srcIndex].first_blk;
    while (currentBlockSrc != FAT_EOF) {
        uint8_t block_data[BLOCK_SIZE];
        disk.read(currentBlockSrc, block_data);

        int16_t freeBlockDest = -1;
        for (int i = 1; i < (BLOCK_SIZE / 2); ++i) {
            if (fat[i] == FAT_FREE) {
                freeBlockDest = i;
                break;
            }
        }

        if (freeBlockDest == -1) {
            std::cerr << "Error: No free blocks left on disk.\n";
            return -1;
        }

        // Link current destination block to the new free block
        fat[currentBlockDest] = freeBlockDest;
        fat[freeBlockDest] = FAT_EOF;

        std::cout << "Linking block " << currentBlockSrc << " from " << filename1 
                  << " to free block " << freeBlockDest << " for " << filename2 << ".\n";

        disk.write(freeBlockDest, block_data);

        currentBlockSrc = fat[currentBlockSrc];
        currentBlockDest = freeBlockDest;
    }

    dir_entries[destIndex].size += dir_entries[srcIndex].size;
    disk.write(ROOT_BLOCK, root_dir_data);
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    std::cout << "Completed appending " << filename1 << " to " << filename2 << ".\n";

    return 0;
}


/* THIS SECTION IS CURRENTLY WIP */


// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirname) {
    std::cout << "FS::mkdir()\n";

    // 1. Read the current directory from disk
    uint8_t current_dir_data[BLOCK_SIZE];
    disk.read(current_directory_block, current_dir_data);  // Changed from ROOT_BLOCK
    struct dir_entry *current_dir_entries = reinterpret_cast<struct dir_entry*>(current_dir_data);

    // 2. Check if the directory name already exists
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(current_dir_entries[i].file_name, dirname.c_str()) == 0) {
            std::cerr << "Directory already exists.\n";
            return -1;
        }
    }

    // 3. Find a free block for the new directory
    int16_t freeBlock = -1;
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
    for (int i = 0; i < (BLOCK_SIZE / 2); ++i) {
        if (fat[i] == FAT_FREE) {
            freeBlock = i;
            break;
        }
    }

    if (freeBlock == -1) {
        std::cerr << "No free blocks left on disk.\n";
        return -1;
    }

    // 4. Write the ".." entry in the new directory block
    struct dir_entry new_dir[BLOCK_SIZE / sizeof(struct dir_entry)];
    strcpy(new_dir[0].file_name, "..");
    new_dir[0].size = 0;  // size is 0 for ".."
    new_dir[0].first_blk = ROOT_BLOCK;  // ".." should point back to the current directory
    new_dir[0].type = TYPE_DIR;
    new_dir[0].access_rights = READ | WRITE;

    for (int i = 1; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
    new_dir[i].file_name[0] = '\0';  // Setting the first character to null indicates unused
    new_dir[i].size = 0;
    new_dir[i].first_blk = -1;  // Indicates no block associated
    }

    disk.write(freeBlock, reinterpret_cast<uint8_t*>(new_dir));

    // 5. Update the current directory with the new directory's entry
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (current_dir_entries[i].file_name[0] == '\0') {
            strcpy(current_dir_entries[i].file_name, dirname.c_str());
            current_dir_entries[i].size = sizeof(struct dir_entry);  // size of one dir_entry (for "..")
            current_dir_entries[i].first_blk = freeBlock;
            current_dir_entries[i].type = TYPE_DIR;
            current_dir_entries[i].access_rights = READ | WRITE;
            break;
        }
    }

    disk.write(current_directory_block, current_dir_data);  // Changed from ROOT_BLOCK

    // 6. Update FAT
    fat[freeBlock] = FAT_EOF;
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    return 0;
}

/* THIS SECTION IS CURRENTLY WIP */


// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirname) {
    std::cout << "FS::cd()\n";

    if (dirname == "/") {
        current_directory_block = ROOT_BLOCK;
        return 0;
    }

    // If dirname is "..", we simply need to fetch the parent directory from the current directory
    if (dirname == "..") {
        uint8_t dir_data[BLOCK_SIZE];
        disk.read(current_directory_block, dir_data);
        struct dir_entry *entries = reinterpret_cast<struct dir_entry*>(dir_data);
        current_directory_block = entries[0].first_blk;  // ".." is always the first entry
        return 0;
    }

    // Read the current directory
    uint8_t current_dir_data[BLOCK_SIZE];
    disk.read(current_directory_block, current_dir_data);
    struct dir_entry *current_dir_entries = reinterpret_cast<struct dir_entry*>(current_dir_data);

    // Search for the directory named dirname
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(current_dir_entries[i].file_name, dirname.c_str()) == 0) {
            if (current_dir_entries[i].type == TYPE_DIR) {
                current_directory_block = current_dir_entries[i].first_blk;
                return 0;  // Successfully changed the directory
            } else {
                std::cerr << dirname << " is not a directory.\n";
                return -1;
            }
        }
    }

    std::cerr << "Directory not found.\n";
    return -1;
}



std::string FS::get_directory_name(unsigned block_no) {
    uint8_t dir_data[BLOCK_SIZE];
    disk.read(block_no, dir_data);
    struct dir_entry *entries = reinterpret_cast<struct dir_entry*>(dir_data);

    unsigned parent_block = entries[0].first_blk;  // ".." points to the parent directory

    uint8_t parent_dir_data[BLOCK_SIZE];
    disk.read(parent_block, parent_dir_data);
    struct dir_entry *parent_entries = reinterpret_cast<struct dir_entry*>(parent_dir_data);

    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (parent_entries[i].first_blk == block_no) {
            return std::string(parent_entries[i].file_name);
        }
    }
    return "";
}



std::string FS::recursive_pwd(unsigned block_no) {
    if (block_no == ROOT_BLOCK) {
        return "/";
    }

    uint8_t dir_data[BLOCK_SIZE];
    disk.read(block_no, dir_data);
    struct dir_entry *entries = reinterpret_cast<struct dir_entry*>(dir_data);

    // Assuming ".." is always the first entry which points to the parent directory
    unsigned parent_block = entries[0].first_blk;

    std::string current_directory_name = get_directory_name(block_no);
    if (current_directory_name.empty()) {
        std::cerr << "Error: Directory name not found.\n";
        return "";
    }

    return recursive_pwd(parent_block) + current_directory_name + "/";
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd() {
    std::cout << "FS::pwd()\n";

    // If we're in the root directory
    if (current_directory_block == ROOT_BLOCK) {
        std::cout << "/\n";
        return 0;
    }
    
    std::string path = recursive_pwd(current_directory_block);
    if (path.back() == '/' && path.length() > 1) {  // Check if the last character is a slash and it's not the root directory
        path.pop_back();  // Remove the last character
    }
    std::cout << path << std::endl;
    return 0;
}


// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}

struct dir_entry* FS::find_directory_entry(std::string name) {
    uint8_t current_dir_data[BLOCK_SIZE];
    disk.read(current_directory_block, current_dir_data);
    struct dir_entry *current_dir_entries = reinterpret_cast<struct dir_entry*>(current_dir_data);

    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(current_dir_entries[i].file_name, name.c_str()) == 0) {
            return &current_dir_entries[i];
        }
    }

    return nullptr; // Return null if the name doesn't exist in the current directory.
}

int FS::find_directory_entry(const std::string& name, dir_entry* entries) {
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (strcmp(entries[i].file_name, name.c_str()) == 0) {
            return i;
        }
    }
    return -1; // Not found
}

int FS::find_free_directory_entry(dir_entry* entries) {
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i) {
        if (entries[i].file_name[0] == '\0') { // Unused entries have an empty filename
            return i;
        }
    }
    return -1; // No free entries
}

int FS::find_free_fat_entry(int start_idx) {
    for (int i = start_idx; i < (BLOCK_SIZE / 2); ++i) { 
        if (fat[i] == FAT_FREE) {
            return i;
        }
    }
    return -1; // No free FAT entries found
}

