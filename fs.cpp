#include <iostream>
#include "fs.h"
#include <string>
#include <cstring>
#include <sstream>

FS::FS()
{
}

FS::~FS()
{
}

// formats the disk, i.e., creates an empty file system
int FS::format()
{
    // Initialize the FAT
    fat[0] = ROOT_BLOCK; // root directory
    fat[1] = FAT_BLOCK;  // FAT
    for (int i = 2; i < disk.get_no_blocks(); i++)
    {
        fat[i] = FAT_FREE;
    }

    // Write FAT to disk
    if (disk.write(FAT_BLOCK, reinterpret_cast<uint8_t *>(fat)) != 0)
    {
        std::cerr << "Error writing FAT to disk.\n";
        return -1; // or other appropriate error code
    }

    // Initialize root directory with "." and ".." entries
    struct dir_entry root_entries[BLOCK_SIZE / sizeof(struct dir_entry)] = {0};
    strcpy(root_entries[0].file_name, "."); // Current directory
    root_entries[0].size = 0;
    root_entries[0].first_blk = ROOT_BLOCK;
    root_entries[0].type = TYPE_DIR;
    root_entries[0].access_rights = READ | WRITE | EXECUTE;

    strcpy(root_entries[1].file_name, ".."); // Parent directory (itself in this case)
    root_entries[1].size = 0;
    root_entries[1].first_blk = ROOT_BLOCK;
    root_entries[1].type = TYPE_DIR;
    root_entries[1].access_rights = READ | WRITE | EXECUTE;

    // Write the initialized root directory to disk
    if (disk.write(ROOT_BLOCK, reinterpret_cast<uint8_t *>(root_entries)) != 0)
    {
        std::cerr << "Error writing root directory to disk.\n";
        return -1; // or other appropriate error code
    }

    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
    
    // 1. Resolve the path
    std::vector<std::string> pathParts = resolve_path(filepath);
    if (pathParts.empty())
    {
        std::cerr << "Invalid path." << std::endl;
        return -1;
    }

    // 2. Navigate to the correct directory
    unsigned int currentBlock = current_directory_block;
    struct dir_entry *dirEntry = nullptr;
    for (size_t i = 0; i < pathParts.size() - 1; ++i)
    { // Navigate through intermediate directories
        dirEntry = find_directory_entry(pathParts[i]);
        if (dirEntry == nullptr || dirEntry->type != TYPE_DIR)
        {
            std::cerr << "Directory not found: " << pathParts[i] << "\n";
            return -1;
        }
        currentBlock = dirEntry->first_blk; // Move to the next directory in the path
    }

    // Now, currentBlock is where the file should be created
    uint8_t dir_data[BLOCK_SIZE];
    disk.read(currentBlock, dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry *>(dir_data);

    // Check if the directory has write permission
    if (!(dir_entries[0].access_rights & WRITE))
    { // The first entry [0] is the directory itself
        std::cerr << "Write permission denied for directory: " << pathParts[pathParts.size() - 2] << "\n";
        return -1;
    }

    std::string filename = pathParts.back(); // The last part is the filename
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
    {
        if (std::string(dir_entries[i].file_name) == filename)
        {
            std::cerr << "File already exists: " << filename << std::endl;
            return -1;
        }
    }

    // Check if filename is too long.
    if (filename.size() > 55)
    {
        std::cerr << "Filename too long: " << filename << ". Max length is 55 characters.\n";
        return -1;
    }
    
    // 1. Find a free block (correct position)
    int free_block = -1;
    for (int i = 2; i < disk.get_no_blocks(); ++i)
    { // Start from 2 because 0 and 1 are reserved
        if (fat[i] == FAT_FREE)
        {
            free_block = i;
            break;
        }
    }

    // If no free blocks found, return error.
    if (free_block == -1)
    {
        std::cerr << "No free blocks available." << std::endl;
        return -1;
    }
    // 2. Create the file (correct position)
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
    while (true)
    {
        std::getline(std::cin, input_line);
        if (input_line.empty())
            break; // Stop if input is an empty row

        // If this line won't fit in the current buffer, write current buffer to disk
        if (buffer_offset + input_line.size() + 1 > BLOCK_SIZE)
        {
            disk.write(free_block, buffer);
            memset(buffer, 0, BLOCK_SIZE); // Clear buffer
            buffer_offset = 0;

            // 5. Handle the case where the file spans multiple blocks.
            // Find another free block
            int next_free_block = -1;
            for (int i = free_block + 1; i < disk.get_no_blocks(); ++i)
            {
                if (fat[i] == FAT_FREE)
                {
                    next_free_block = i;
                    break;
                }
            }
            if (next_free_block == -1)
            {
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
    if (buffer_offset > 0)
    {
        disk.write(free_block, buffer);
    }

    // Set the FAT entry for the last block to EOF
    fat[free_block] = FAT_EOF;

    // Update the directory
    int freeslot = -1;
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
    {
        if (dir_entries[i].file_name[0] == '\0')
        { // Check for empty slot
            dir_entries[i] = new_entry;
            freeslot = 1;
            break;
        }
    }
    if (freeslot == -1){
        std::cerr << "Error: It is full, you can not create a new file." << std::endl;
    } 


    // Write back the updated directory and FAT to the disk
    disk.write(currentBlock, dir_data); // Use currentBlock instead of current_directory_block
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t *>(fat));

    return 0;
}

int FS::cat(std::string filepath)
{
    // 1. Resolve the path
    std::vector<std::string> pathParts = resolve_path(filepath);
    if (pathParts.empty())
    {
        std::cerr << "Invalid path.\n";
        return -1;
    }

    // 2. Find the directory entry of the file
    unsigned int currentBlock = current_directory_block;
    struct dir_entry *dirEntry = nullptr;
    for (size_t i = 0; i < pathParts.size() - 1; ++i)
    { // Navigate through intermediate directories
        dirEntry = find_directory_entry(pathParts[i]);
        if (dirEntry == nullptr || dirEntry->type != TYPE_DIR)
        {
            std::cerr << "Directory not found: " << pathParts[i] << "\n";
            return -1;
        }
        currentBlock = dirEntry->first_blk; // Move to the next directory in the path
    }

    // Now, currentBlock is where the file should be
    uint8_t dir_data[BLOCK_SIZE];
    disk.read(currentBlock, dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry *>(dir_data);
    int fileIndex = find_directory_entry(pathParts.back(), dir_entries); // Find the file in the directory
    if (fileIndex == -1)
    {
        std::cerr << "File not found: " << pathParts.back() << "\n";
        return -1;
    }

    dirEntry = &dir_entries[fileIndex]; // Get the directory entry of the file

    // Check if the entry is a directory
    if (dirEntry->type == TYPE_DIR)
    {
        std::cerr << "Cannot use cat on a directory: " << pathParts.back() << "\n";
        return -1;
    }

    // Check if the file has read permission
    if (!(dirEntry->access_rights & READ))
    {
        std::cerr << "Read permission denied for file: " << pathParts.back() << "\n";
        return -1;
    }

    // 3. Read the FAT
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t *>(fat));

    // Reuse currentBlock for reading the file content
    currentBlock = dirEntry->first_blk; // Make sure the type of currentBlock can accommodate this value
    while (currentBlock != FAT_EOF)
    {
        uint8_t block_data[BLOCK_SIZE];
        disk.read(currentBlock, block_data);

        // Print the content of the block as C-strings
        char *ptr = (char *)block_data;
        while (*ptr)
        {                                  // Loop until we hit a null character
            std::cout << ptr << std::endl; // Print the string
            ptr += strlen(ptr) + 1;        // Move to the next string in the block
        }

        currentBlock = fat[currentBlock]; // This may need type casting if types are not compatible
    }
    return 0;
}

// ls lists the content in the current directory (files and sub-directories)
int FS::ls()
{
    // 1. Read the current directory from disk
    uint8_t current_dir_data[BLOCK_SIZE];
    disk.read(current_directory_block, current_dir_data); // Changed from ROOT_BLOCK
    struct dir_entry *current_dir_entries = reinterpret_cast<struct dir_entry *>(current_dir_data);

    // 2. Iterate over all entries and print details
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
    {
        struct dir_entry *entry = &current_dir_entries[i];
        if (entry->file_name[0] != '\0')
        { // valid entry
            std::cout << entry->file_name << "\t";
            // TYPE
            if (entry->type == TYPE_DIR)
            {
                std::cout << "dir\t";
            }
            else
            {
                std::cout << "file\t";
            }

            // ACCESS RIGHTS
            if (entry->access_rights & 0x04)
            {
                std::cout << "r";
            }
            else
            {
                std::cout << "-";
            }

            if (entry->access_rights & 0x02)
            {
                std::cout << "w";
            }
            else
            {
                std::cout << "-";
            }

            if (entry->access_rights & 0x01)
            {
                std::cout << "x\t";
            }
            else
            {
                std::cout << "-\t";
            }

            // SIZE
            if (entry->size == 0 || entry->type == TYPE_DIR)
            {
                std::cout << "-\n";
            }
            else
            {
                std::cout << entry->size << "\n";
            }
        }
    }
    return 0;
}
int FS::cp(std::string sourcepath, std::string destpath)
{
    // 1. Resolve source & destination paths (keeping "..")
    std::vector<std::string> sourcePathParts = resolve_path_for_cp_and_mv(sourcepath);
    std::vector<std::string> destPathParts   = resolve_path_for_cp_and_mv(destpath);

    // Backup our current directory
    unsigned int backupCDB = current_directory_block;

    // 2. Locate the source FILE
    unsigned int srcBlock = current_directory_block;
    if (!sourcePathParts.empty())
    {
        // All but the last part must be directories
        for (int i = 0; i < (int)sourcePathParts.size() - 1; i++)
        {
            const std::string &dirName = sourcePathParts[i];

            current_directory_block = srcBlock;
            struct dir_entry* subdir = find_directory_entry(dirName);
            if (!subdir || subdir->type != TYPE_DIR)
            {
                std::cerr << "Source path invalid or not a directory: " << dirName << "\n";
                current_directory_block = backupCDB;
                return -1;
            }
            srcBlock = subdir->first_blk;
        }
    }

    // The last part is our file name
    current_directory_block = srcBlock;
    if (sourcePathParts.empty())
    {
        std::cerr << "No source file specified.\n";
        current_directory_block = backupCDB;
        return -1;
    }

    const std::string sourceFileName = sourcePathParts.back();

    struct dir_entry *sourceFileEntry = find_directory_entry(sourceFileName);
    if (!sourceFileEntry || sourceFileEntry->type != TYPE_FILE)
    {
        std::cerr << "Source file not found or is a directory.\n";
        current_directory_block = backupCDB;
        return -1;
    }

    // Read the FAT so we can follow block chains
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));

    uint32_t sourceSize = sourceFileEntry->size;
    unsigned int sourceBlk = sourceFileEntry->first_blk;

    // 3. Read source file contents into a buffer
    uint8_t* sourceData = new uint8_t[sourceSize];
    memset(sourceData, 0, sourceSize);

    unsigned int blockToRead = sourceBlk;
    int bytesRead = 0;
    while (blockToRead != FAT_EOF && bytesRead < (int)sourceSize)
    {
        uint8_t tempBuf[BLOCK_SIZE];
        disk.read(blockToRead, tempBuf);
        for (int i = 0; i < BLOCK_SIZE && bytesRead < (int)sourceSize; i++)
        {
            sourceData[bytesRead++] = tempBuf[i];
        }
        blockToRead = fat[blockToRead];
    }

    // Restore current directory to what it was before finding the source
    current_directory_block = backupCDB;

    // 4. Navigate the DESTINATION path
    unsigned int destBlock = current_directory_block;

    // Check if the destination path is absolute
    if (destpath[0] == '/') {
        current_directory_block = 0; //root directory is at block 0

        // Re-initialize destBlock to the root directory for absolute path handling
        destBlock = current_directory_block;
    }

    // We keep track of all components *except possibly the last one* as directories
    // Then decide if the last piece is a directory or a filename
    std::string finalComponent;
    if (!destPathParts.empty())
    {
        finalComponent = destPathParts.back(); // we may not treat this as a subdir
        // We'll only pop it if we suspect it's a filename.
    }

    // We handle everything except the final component as directories
    int lastIndex = (int)destPathParts.size() - 1;
    if (lastIndex < 0) lastIndex = 0; // safety

    for (int i = 0; i < lastIndex; i++)  
    {
        const std::string &part = destPathParts[i];

        // If part == "..", go up
        if (part == "..")
        {
            // Move up
            uint8_t dirData[BLOCK_SIZE];
            disk.read(destBlock, dirData);
            struct dir_entry* entries = reinterpret_cast<struct dir_entry*>(dirData);

            int ddIndex = find_directory_entry("..", entries);
            if (ddIndex != -1) {
                destBlock = entries[ddIndex].first_blk; // parent
            } else {
                std::cerr << "Cannot move up from block " << destBlock << "\n";
            }
        }
        else
        {
            // Move into subdir named part
            current_directory_block = destBlock;
            struct dir_entry* subdir = find_directory_entry(part);
            if (!subdir || subdir->type != TYPE_DIR) {
                std::cerr << "Destination path invalid or not a directory: " 
                        << part << "\n";
                delete[] sourceData;
                current_directory_block = backupCDB;
                return -1;
            }
            destBlock = subdir->first_blk;
        }
    }


    // Now we handle the final component
    std::string destFileName; // we'll fill this

    if (!destPathParts.empty())
    {
        // last component
        if (finalComponent == "..")
        {
            // Move up
            uint8_t dirData[BLOCK_SIZE];
            disk.read(destBlock, dirData);
            struct dir_entry* entries = reinterpret_cast<struct dir_entry*>(dirData);

            int ddIndex = find_directory_entry("..", entries);
            if (ddIndex != -1) {
                destBlock = entries[ddIndex].first_blk;
            }
            // Reuse the sourceFileName
            destFileName = sourceFileName;
        }
        else
        {
            // Check if the final component is a subdir in the current 'destBlock'
            current_directory_block = destBlock;
            struct dir_entry* maybeDir = find_directory_entry(finalComponent);
            if (maybeDir && maybeDir->type == TYPE_DIR)
            {
                // We put the new file inside that directory
                destBlock = maybeDir->first_blk;

                // Reuse sourceFileName
                destFileName = sourceFileName;
            }
            else
            {
                // We interpret it as the new filename
                destFileName = finalComponent;
            }
        }
    }
    else
    {
        // Means user typed no final path part => reuse source
        destFileName = sourceFileName;
    }

    // 5. Ensure there's no collision & allocate new FAT chain
    // Read the directory
    uint8_t destDirData[BLOCK_SIZE];
    disk.read(destBlock, destDirData);
    struct dir_entry* destEntries = reinterpret_cast<struct dir_entry*>(destDirData);

    // Check collision
    for (int i = 0; i < (BLOCK_SIZE / sizeof(dir_entry)); i++)
    {
        if (strcmp(destEntries[i].file_name, destFileName.c_str()) == 0)
        {
            std::cerr << "Destination file already exists: " << destFileName << std::endl;
            delete[] sourceData;
            current_directory_block = backupCDB;
            return -1;
        }
    }

    // Allocate blocks
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat));
    int firstFreeBlock = find_free_fat_entry(2);
    if (firstFreeBlock == -1)
    {
        std::cerr << "No free blocks to copy file.\n";
        delete[] sourceData;
        current_directory_block = backupCDB;
        return -1;
    }

    int lastBlock = -1;
    int currentFreeBlock = firstFreeBlock;
    bytesRead = 0;

    while (bytesRead < (int)sourceSize)
    {
        if (lastBlock != -1)
        {
            fat[lastBlock] = currentFreeBlock;
        }

        uint8_t writeBuf[BLOCK_SIZE];
        memset(writeBuf, 0, BLOCK_SIZE);
        int idx = 0;
        while (idx < BLOCK_SIZE && bytesRead < (int)sourceSize)
        {
            writeBuf[idx++] = sourceData[bytesRead++];
        }

        disk.write(currentFreeBlock, writeBuf);

        if (bytesRead == (int)sourceSize)
        {
            fat[currentFreeBlock] = FAT_EOF;
            break;
        }

        lastBlock = currentFreeBlock;
        currentFreeBlock = find_free_fat_entry(currentFreeBlock + 1);
        if (currentFreeBlock == -1)
        {
            std::cerr << "Ran out of blocks while copying.\n";
            delete[] sourceData;
            current_directory_block = backupCDB;
            return -1;
        }
    }

    // 6. Create a new directory entry
    int freeDirIndex = find_free_directory_entry(destEntries);
    if (freeDirIndex == -1)
    {
        std::cerr << "Destination directory is full.\n";
        delete[] sourceData;
        current_directory_block = backupCDB;
        return -1;
    }

    dir_entry newFile;
    memset(&newFile, 0, sizeof(newFile));
    strncpy(newFile.file_name, destFileName.c_str(), sizeof(newFile.file_name)-1);
    newFile.file_name[sizeof(newFile.file_name)-1] = '\0';

    newFile.size        = sourceSize;
    newFile.first_blk   = firstFreeBlock;
    newFile.type        = TYPE_FILE;
    // Copy the source file's permissions
    newFile.access_rights = sourceFileEntry->access_rights;

    destEntries[freeDirIndex] = newFile;

    // Write updated dir & FAT
    disk.write(destBlock, destDirData);
    uint8_t fatBuf[BLOCK_SIZE];
    memcpy(fatBuf, fat, BLOCK_SIZE);
    disk.write(FAT_BLOCK, fatBuf);

    delete[] sourceData;
    current_directory_block = backupCDB;
    return 0;
}



int FS::mv(std::string sourcepath, std::string destpath)
{
    // 1. Resolve paths to their components
    std::vector<std::string> sourcePathParts = resolve_path(sourcepath);
    std::vector<std::string> destPathParts = resolve_path(destpath);

    // Backup the current directory block for restoration later
    unsigned int backupCurrentDirectoryBlock = current_directory_block;

    // Find the directory entry for the source file
    unsigned int currentBlock = (sourcepath[0] == '/') ? ROOT_BLOCK : current_directory_block;
    struct dir_entry *sourceDirEntry = nullptr;
    for (const auto &part : sourcePathParts)
    {
        current_directory_block = currentBlock; // Update current directory
        sourceDirEntry = find_directory_entry(part);
        if (sourceDirEntry == nullptr || (sourceDirEntry->type != TYPE_DIR && part != sourcePathParts.back()))
        {
            std::cerr << "Source path invalid or not a directory.\n";
            current_directory_block = backupCurrentDirectoryBlock;
            return -1;
        }
        if (part != sourcePathParts.back())
        {
            currentBlock = sourceDirEntry->first_blk;
        }
    }

    // Now, currentBlock is where the source file should be
    uint8_t source_dir_data[BLOCK_SIZE];
    disk.read(currentBlock, source_dir_data);
    struct dir_entry *source_dir_entries = reinterpret_cast<struct dir_entry *>(source_dir_data);

    // Check write permission on the source directory (for delete)
    if (!(source_dir_entries[0].access_rights & WRITE))
    { // the first entry [0] is the directory itself
        std::cerr << "Write permission denied for source directory.\n";
        current_directory_block = backupCurrentDirectoryBlock;
        return -1;
    }

    int sourceIndex = find_directory_entry(sourcePathParts.back(), source_dir_entries);
    if (sourceIndex == -1)
    {
        std::cerr << "Source file not found.\n";
        current_directory_block = backupCurrentDirectoryBlock;
        return -1;
    }

    // Find the directory for the destination path
    currentBlock = (destpath[0] == '/') ? ROOT_BLOCK : current_directory_block;
    struct dir_entry *destDirEntry = nullptr;
    std::string destFileName = destPathParts.back(); // Extract the filename from the destination path
    destPathParts.pop_back();                        // Remove the filename part, leaving only the directory path
    for (const auto &part : destPathParts)
    {
        current_directory_block = currentBlock; // Update current directory
        destDirEntry = find_directory_entry(part);
        if (destDirEntry == nullptr)
        {
            std::cerr << "Destination path invalid or directory does not exist.\n";
            current_directory_block = backupCurrentDirectoryBlock;
            return -1;
        }
        currentBlock = destDirEntry->first_blk;
    }

    // Now we already have the destination directory entry, use it to check if it's a directory
    uint8_t dest_dir_data[BLOCK_SIZE];
    disk.read(currentBlock, dest_dir_data);
    struct dir_entry *dest_dir_entries = reinterpret_cast<struct dir_entry *>(dest_dir_data);

    bool destinationIsDir = false;
    // Check if destination entry already exists (if it's a file)
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); i++)
    {
        if (strcmp(dest_dir_entries[i].file_name, destFileName.c_str()) == 0)
        {
            if (dest_dir_entries[i].type == TYPE_DIR)
            {
                destinationIsDir = true;
                break; // Found directory
            }
            else
            {
                std::cerr << "Destination file already exists: " << destFileName << std::endl;
                current_directory_block = backupCurrentDirectoryBlock;
                return -1; // File already exists
            }
        }
    }

    // Now, decide based on destination being a directory or not
    if (destinationIsDir)
    {
        // Proceed with moving the file to the directory
        return mv_to_dir(sourcepath, destpath);
    }

 
    // Now, currentBlock is where the destination directory is
    disk.read(currentBlock, dest_dir_data);

    // Check if the destination file already exists
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); i++)
    {
        if (strcmp(dest_dir_entries[i].file_name, destFileName.c_str()) == 0)
        {
            std::cerr << "Destination file already exists: " << destFileName << std::endl;
            current_directory_block = backupCurrentDirectoryBlock;
            return -1; // File already exists
        }
    }

    // Check write permission on the destination directory (for add)
    if (!(dest_dir_entries[0].access_rights & WRITE))
    { // the first entry [0] is the directory itself
        std::cerr << "Write permission denied for destination directory.\n";
        current_directory_block = backupCurrentDirectoryBlock;
        return -1;
    }

    int destIndex = find_free_directory_entry(dest_dir_entries);
    if (destIndex == -1 && destPathParts.size() != 0)
    { // If we're moving (not just renaming in the same directory)
        std::cerr << "Destination directory is full. Cannot move file.\n";
        current_directory_block = backupCurrentDirectoryBlock;
        return -1;
    }

    if (destPathParts.size() == 0)
    { // Renaming in the same directory
        strncpy(source_dir_entries[sourceIndex].file_name, destFileName.c_str(), sizeof(source_dir_entries[sourceIndex].file_name) - 1);
        source_dir_entries[sourceIndex].file_name[sizeof(source_dir_entries[sourceIndex].file_name) - 1] = '\0'; // Ensure null-termination
        // Write back the modified directory entry to the disk
        disk.write(backupCurrentDirectoryBlock, source_dir_data); // Write back to the source directory
    }
    else
    {                                                                  // Moving to a different directory
        dest_dir_entries[destIndex] = source_dir_entries[sourceIndex]; // Copy the entry
        strncpy(dest_dir_entries[destIndex].file_name, destFileName.c_str(), sizeof(dest_dir_entries[destIndex].file_name) - 1);
        dest_dir_entries[destIndex].file_name[sizeof(dest_dir_entries[destIndex].file_name) - 1] = '\0'; // Ensure null-termination
        source_dir_entries[sourceIndex].file_name[0] = '\0';                                             // Mark the source entry as deleted
        // Write back the modified directory entries to the disk
        disk.write(currentBlock, dest_dir_data);                  // Destination directory
        disk.write(backupCurrentDirectoryBlock, source_dir_data); // Source directory
    }

    current_directory_block = backupCurrentDirectoryBlock; // Restore original current directory
    return 0;
}

int FS::mv_to_dir(std::string sourcepath, std::string destpath)
{
    //1. Resolve source and destination paths
    std::vector<std::string> sourcePathParts = resolve_path(sourcepath);
    std::vector<std::string> destPathParts = resolve_path(destpath);

    unsigned int backupCurrentDirectoryBlock = current_directory_block;

    // Locate source file/directory
    unsigned int sourceBlock = (sourcepath[0] == '/') ? ROOT_BLOCK : current_directory_block;
    struct dir_entry *sourceDirEntry = nullptr;
    for (const auto &part : sourcePathParts)
    {
        current_directory_block = sourceBlock;
        sourceDirEntry = find_directory_entry(part);
        if (!sourceDirEntry || (sourceDirEntry->type != TYPE_DIR && part != sourcePathParts.back()))
        {
            std::cerr << "Invalid source path or not a directory.\n";
            current_directory_block = backupCurrentDirectoryBlock;
            return -1;
        }
        if (part != sourcePathParts.back())
        {
            sourceBlock = sourceDirEntry->first_blk;
        }
    }

    // Read source directory data
    uint8_t source_dir_data[BLOCK_SIZE];
    disk.read(sourceBlock, source_dir_data);
    struct dir_entry *source_dir_entries = reinterpret_cast<struct dir_entry *>(source_dir_data);

    int sourceIndex = find_directory_entry(sourcePathParts.back(), source_dir_entries);
    if (sourceIndex == -1)
    {
        std::cerr << "Source file/directory not found.\n";
        current_directory_block = backupCurrentDirectoryBlock;
        return -1;
    }

    //Locate the destination dir
    unsigned int destBlock = (destpath[0] == '/') ? ROOT_BLOCK : current_directory_block;
    struct dir_entry *destDirEntry = nullptr;
    for (const auto &part : destPathParts)
    {
        current_directory_block = destBlock;
        destDirEntry = find_directory_entry(part);
        
        if (!destDirEntry || destDirEntry->type != TYPE_DIR)
        {
            std::cerr << "Invalid destination path or not a directory.\n";
            current_directory_block = backupCurrentDirectoryBlock;
            return -1;
        }
        destBlock = destDirEntry->first_blk;
    }

    //Read the destination directory data
    uint8_t dest_dir_data[BLOCK_SIZE];
    disk.read(destBlock, dest_dir_data);
    struct dir_entry *dest_dir_entries = reinterpret_cast<struct dir_entry *>(dest_dir_data);

    //Check for an existing entry in the destination
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); i++)
    {
        if (strcmp(dest_dir_entries[i].file_name, sourcePathParts.back().c_str()) == 0)
        {
            std::cerr << "File or directory already exists in destination.\n";
            current_directory_block = backupCurrentDirectoryBlock;
            return -1;
        }
    }

    // Check write permission on the destination directory (for add)
    if (!(dest_dir_entries[0].access_rights & WRITE))
    { // the first entry [0] is the directory itself
        std::cerr << "Write permission denied for destination directory.\n";
        current_directory_block = backupCurrentDirectoryBlock;
        return -1;
    }

    //Move the source entry to the destination
    int destIndex = find_free_directory_entry(dest_dir_entries);
    if (destIndex == -1)
    {
        std::cerr << "No space in destination directory.\n";
        current_directory_block = backupCurrentDirectoryBlock;
        return -1;
    }

    dest_dir_entries[destIndex] = source_dir_entries[sourceIndex];
    strncpy(dest_dir_entries[destIndex].file_name, sourcePathParts.back().c_str(), sizeof(dest_dir_entries[destIndex].file_name) - 1);
    dest_dir_entries[destIndex].file_name[sizeof(dest_dir_entries[destIndex].file_name) - 1] = '\0';
    source_dir_entries[sourceIndex].file_name[0] = '\0';

    //Write updates to disk
    disk.write(destBlock, dest_dir_data);
    disk.write(sourceBlock, source_dir_data);

    current_directory_block = backupCurrentDirectoryBlock;
    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{

    // 1. Resolve paths to their components
    std::vector<std::string> pathParts = resolve_path(filepath);
    if (pathParts.empty())
    {
        std::cerr << "Invalid path." << std::endl;
        return -1;
    }

    // Backup the current directory block for restoration later
    unsigned int backupCurrentDirectoryBlock = current_directory_block;

    // Find the directory of the file/directory to be removed
    unsigned int currentBlock = current_directory_block;
    struct dir_entry *dirEntry = nullptr;
    for (size_t i = 0; i < pathParts.size() - 1; ++i)
    {
        current_directory_block = currentBlock; // Update current directory
        dirEntry = find_directory_entry(pathParts[i]);
        if (dirEntry == nullptr || dirEntry->type != TYPE_DIR)
        {
            std::cerr << "Directory not found: " << pathParts[i] << "\n";
            current_directory_block = backupCurrentDirectoryBlock;
            return -1;
        }
        currentBlock = dirEntry->first_blk; // Move to the next directory in the path
    }

    // Now, currentBlock is where the file/directory to be removed should be
    uint8_t dir_data[BLOCK_SIZE];
    disk.read(currentBlock, dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry *>(dir_data);

    // Check write permission on the directory containing the file/directory to be removed
    if (!(dir_entries[0].access_rights & WRITE))
    { // the first entry [0] is the directory itself
        std::cerr << "Write permission denied for directory: " << pathParts[pathParts.size() - 2] << "\n";
        current_directory_block = backupCurrentDirectoryBlock;
        return -1;
    }

    std::string targetName = pathParts.back(); // The last part is the name of the file/directory to be removed
    int entryIndex = find_directory_entry(targetName, dir_entries);
    if (entryIndex == -1)
    {
        std::cerr << "Entry not found.\n";
        current_directory_block = backupCurrentDirectoryBlock;
        return -1;
    }

    struct dir_entry target = dir_entries[entryIndex];

    // If the target is a directory, ensure it's empty
    if (target.type == TYPE_DIR)
    {
        uint8_t dir_data[BLOCK_SIZE];
        disk.read(target.first_blk, dir_data);
        struct dir_entry *entries = reinterpret_cast<struct dir_entry *>(dir_data);
        for (int i = 1; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
        { // Start from 1 to skip ".." entry
            if (entries[i].file_name[0] != '\0')
            {
                std::cerr << "Error: Directory is not empty.\n";
                current_directory_block = backupCurrentDirectoryBlock;
                return -1;
            }
        }

        // If directory is empty, mark its block as free in the FAT
        disk.read(FAT_BLOCK, reinterpret_cast<uint8_t *>(fat));
        fat[target.first_blk] = FAT_FREE;
        disk.write(FAT_BLOCK, reinterpret_cast<uint8_t *>(fat));
    }
    // If it's a file, mark its blocks as free in the FAT
    else
    {
        int16_t currentBlock = target.first_blk;
        while (currentBlock != FAT_EOF)
        {
            disk.read(FAT_BLOCK, reinterpret_cast<uint8_t *>(fat));
            int16_t nextBlock = fat[currentBlock];
            fat[currentBlock] = FAT_FREE;
            currentBlock = nextBlock;
            disk.write(FAT_BLOCK, reinterpret_cast<uint8_t *>(fat));
        }
    }

    // Remove its directory entry
    memset(&dir_entries[entryIndex], 0, sizeof(struct dir_entry));

    // Write back the modified directory to the disk
    disk.write(currentBlock, dir_data); // Write to the actual directory, not just the current directory

    current_directory_block = backupCurrentDirectoryBlock; // Restore original current directory
    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{

    // Resolve paths to their components
    std::vector<std::string> pathParts1 = resolve_path(filepath1);
    std::vector<std::string> pathParts2 = resolve_path(filepath2);

    if (pathParts1.empty() || pathParts2.empty())
    {
        std::cerr << "Invalid path.\n";
        return -1;
    }

    // Find the directory entry of the source file
    unsigned int currentBlock1 = current_directory_block;
    struct dir_entry *dirEntry1 = nullptr;
    for (size_t i = 0; i < pathParts1.size() - 1; ++i)
    {
        dirEntry1 = find_directory_entry(pathParts1[i]);
        if (dirEntry1 == nullptr || dirEntry1->type != TYPE_DIR)
        {
            std::cerr << "Directory not found in source path: " << pathParts1[i] << "\n";
            return -1;
        }
        currentBlock1 = dirEntry1->first_blk;
    }

    // Find the directory entry of the destination file
    unsigned int currentBlock2 = current_directory_block;
    struct dir_entry *dirEntry2 = nullptr;
    for (size_t i = 0; i < pathParts2.size() - 1; ++i)
    {
        dirEntry2 = find_directory_entry(pathParts2[i]);
        if (dirEntry2 == nullptr || dirEntry2->type != TYPE_DIR)
        {
            std::cerr << "Directory not found in destination path: " << pathParts2[i] << "\n";
            return -1;
        }
        currentBlock2 = dirEntry2->first_blk;
    }

    // Now, currentBlock1 is where the source file should be
    uint8_t dir_data1[BLOCK_SIZE];
    disk.read(currentBlock1, dir_data1);
    struct dir_entry *dir_entries1 = reinterpret_cast<struct dir_entry *>(dir_data1);
    int fileIndex1 = find_directory_entry(pathParts1.back(), dir_entries1);
    if (fileIndex1 == -1)
    {
        std::cerr << "Source file not found: " << pathParts1.back() << "\n";
        return -1;
    }

    dirEntry1 = &dir_entries1[fileIndex1]; // Get the directory entry of the source file

    // Check read permission on the source file
    if (!(dirEntry1->access_rights & READ))
    {
        std::cerr << "Read permission denied for source file: " << pathParts1.back() << "\n";
        return -1;
    }

    // Now, currentBlock2 is where the destination file should be
    uint8_t dir_data2[BLOCK_SIZE];
    disk.read(currentBlock2, dir_data2);
    struct dir_entry *dir_entries2 = reinterpret_cast<struct dir_entry *>(dir_data2);
    int fileIndex2 = find_directory_entry(pathParts2.back(), dir_entries2);
    if (fileIndex2 == -1)
    {
        std::cerr << "Destination file not found: " << pathParts2.back() << "\n";
        return -1;
    }

    dirEntry2 = &dir_entries2[fileIndex2]; // Get the directory entry of the destination file

    // Check write permission on the destination file
    if (!(dirEntry2->access_rights & (READ | WRITE)))
    {
        std::cerr << "Read and write permission denied for destination file: " << pathParts2.back() << "\n";
        return -1;
    }

    // Read the FAT
    disk.read(FAT_BLOCK, reinterpret_cast<uint8_t *>(fat));

    int16_t currentBlockDest = dirEntry2->first_blk;
    while (fat[currentBlockDest] != FAT_EOF)
    {
        currentBlockDest = fat[currentBlockDest];
    }
    // Find the last block of the destination file and the position to start writing in it
    uint8_t lastBlockData[BLOCK_SIZE];
    disk.read(currentBlockDest, lastBlockData);
    int positionInLastBlock = dirEntry2->size % BLOCK_SIZE;

    // Read the content of the source file
    int16_t currentBlockSrc = dirEntry1->first_blk;
    uint8_t block_data[BLOCK_SIZE];
    int bytesRead = 0;
    while (currentBlockSrc != FAT_EOF)
    {
        disk.read(currentBlockSrc, block_data);

        for (int i = 0; i < BLOCK_SIZE; ++i)
        {
            // Check if we need a new block for the destination file
            if (positionInLastBlock == BLOCK_SIZE)
            {
                // Find a free block
                int16_t freeBlockDest = find_free_fat_entry();
                if (freeBlockDest == -1)
                {
                    std::cerr << "No free blocks left on disk.\n";
                    return -1;
                }

                // Update FAT for the destination file
                fat[currentBlockDest] = freeBlockDest;
                fat[freeBlockDest] = FAT_EOF;

                // Write the current last block to disk
                disk.write(currentBlockDest, lastBlockData);

                // Reset lastBlockData and positionInLastBlock
                memset(lastBlockData, 0, BLOCK_SIZE);
                positionInLastBlock = 0;
                currentBlockDest = freeBlockDest;
            }

            // Write byte from source file to destination file
            lastBlockData[positionInLastBlock] = block_data[i];
            positionInLastBlock++;
            dirEntry2->size++;

            // Break if we have read all bytes of the source file
            if (++bytesRead == dirEntry1->size)
            {
                break;
            }
        }

        // Go to the next block of the source file
        currentBlockSrc = fat[currentBlockSrc];
    }

    // Write the last block of the destination file to disk
    disk.write(currentBlockDest, lastBlockData);

    // Update the size of the destination file in its directory entry
    dir_entries2[fileIndex2].size = dirEntry2->size;

    // Write back the updated directory entries and FAT to the disk
    disk.write(currentBlock2, dir_data2); // Write to the destination directory
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t *>(fat));

    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{

    // Resolve the path to get the parts
    std::vector<std::string> parts = resolve_path(dirpath);
    unsigned parent_block = (dirpath[0] == '/') ? ROOT_BLOCK : current_directory_block;

    std::string dirname = parts.back(); // The last part is the directory to create
    parts.pop_back();                   // Remove the last part as it's the new directory name

    // Find the block of the parent directory
    for (const std::string &part : parts)
    {
        uint8_t dir_data[BLOCK_SIZE];
        disk.read(parent_block, dir_data);
        struct dir_entry *entries = reinterpret_cast<struct dir_entry *>(dir_data);

        bool found = false;
        for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
        {
            if (entries[i].type == TYPE_DIR && part == entries[i].file_name)
            {
                parent_block = entries[i].first_blk;
                found = true;
                break;
            }
        }

        if (!found)
        {
            std::cerr << "Parent directory " << part << " not found.\n";
            return -1;
        }
    }

    // Read the parent directory from disk
    uint8_t parent_dir_data[BLOCK_SIZE];
    disk.read(parent_block, parent_dir_data);
    struct dir_entry *parent_dir_entries = reinterpret_cast<struct dir_entry *>(parent_dir_data);

    // Check if the directory name already exists in the parent directory
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
    {
        if (strcmp(parent_dir_entries[i].file_name, dirname.c_str()) == 0)
        {
            std::cerr << "Directory already exists.\n";
            return -1;
        }
    }

    // Check write permission on the parent directory
    if (!(parent_dir_entries[0].access_rights & WRITE))
    { // the first entry [0] is the directory itself
        std::cerr << "Write permission denied for directory: " << parts[parts.size() - 1] << "\n";
        return -1;
    }

    // Find a free block for the new directory
    int16_t freeBlock = find_free_fat_entry(2); // Start searching from block 2

    if (freeBlock == -1)
    {
        std::cerr << "No free blocks left on disk.\n";
        return -1;
    }

    // Write the ".." entry in the new directory block
    struct dir_entry new_dir[BLOCK_SIZE / sizeof(struct dir_entry)];
    strcpy(new_dir[0].file_name, "..");
    new_dir[0].size = 0;                 // size is 0 for ".."
    new_dir[0].first_blk = parent_block; // ".." should point back to the parent directory
    new_dir[0].type = TYPE_DIR;
    new_dir[0].access_rights = READ | WRITE;

    for (int i = 1; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
    {
        new_dir[i].file_name[0] = '\0'; // Setting the first character to null indicates unused
        new_dir[i].size = 0;
        new_dir[i].first_blk = -1; // Indicates no block associated
    }

    disk.write(freeBlock, reinterpret_cast<uint8_t *>(new_dir));

    // Update the parent directory with the new directory's entry
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
    {
        if (parent_dir_entries[i].file_name[0] == '\0')
        {
            strcpy(parent_dir_entries[i].file_name, dirname.c_str());
            parent_dir_entries[i].size = sizeof(struct dir_entry); // size of one dir_entry (for "..")
            parent_dir_entries[i].first_blk = freeBlock;
            parent_dir_entries[i].type = TYPE_DIR;
            parent_dir_entries[i].access_rights = READ | WRITE;
            break;
        }
    }

    disk.write(parent_block, parent_dir_data);

    // Update FAT
    fat[freeBlock] = FAT_EOF;
    disk.write(FAT_BLOCK, reinterpret_cast<uint8_t *>(fat));

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{

    if (dirpath == "/")
    {
        current_directory_block = ROOT_BLOCK;
        return 0;
    }

    std::vector<std::string> parts = resolve_path(dirpath);
    unsigned block_to_search = (dirpath[0] == '/') ? ROOT_BLOCK : current_directory_block;

    for (const std::string &part : parts)
    {
        uint8_t dir_data[BLOCK_SIZE];
        disk.read(block_to_search, dir_data);
        struct dir_entry *entries = reinterpret_cast<struct dir_entry *>(dir_data);

        if (part == "..")
        {
            // Log before navigating to parent
            int dotdot_index = find_directory_entry("..", entries);
            if (dotdot_index == -1)
            {
                std::cerr << "Parent directory entry not found.\n";
                return -1;
            }
            block_to_search = entries[dotdot_index].first_blk;
            continue;
        }

        bool found = false;
        for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
        {
            if (entries[i].type == TYPE_DIR && part == entries[i].file_name)
            {
                block_to_search = entries[i].first_blk;
                found = true;
                break;
            }
        }

        if (!found)
        {
            std::cerr << "Directory " << part << " not found.\n";
            return -1;
        }
    }

    current_directory_block = block_to_search;

    return 0;
}

std::string FS::get_directory_name(unsigned block_no)
{
    uint8_t dir_data[BLOCK_SIZE];
    disk.read(block_no, dir_data);
    struct dir_entry *entries = reinterpret_cast<struct dir_entry *>(dir_data);

    unsigned parent_block = entries[0].first_blk; // ".." points to the parent directory

    uint8_t parent_dir_data[BLOCK_SIZE];
    disk.read(parent_block, parent_dir_data);
    struct dir_entry *parent_entries = reinterpret_cast<struct dir_entry *>(parent_dir_data);

    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
    {
        if (parent_entries[i].first_blk == block_no)
        {
            return std::string(parent_entries[i].file_name);
        }
    }
    return "";
}

std::string FS::recursive_pwd(unsigned block_no)
{
    if (block_no == ROOT_BLOCK)
    {
        return "/";
    }

    uint8_t dir_data[BLOCK_SIZE];
    disk.read(block_no, dir_data);
    struct dir_entry *entries = reinterpret_cast<struct dir_entry *>(dir_data);

    // Assuming ".." is always the first entry which points to the parent directory
    unsigned parent_block = entries[0].first_blk;

    std::string current_directory_name = get_directory_name(block_no);
    if (current_directory_name.empty())
    {
        std::cerr << "Error: Directory name not found.\n";
        return "";
    }

    return recursive_pwd(parent_block) + current_directory_name + "/";
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
    // If we're in the root directory
    if (current_directory_block == ROOT_BLOCK)
    {
        std::cout << "/\n";
        return 0;
    }

    std::string path = recursive_pwd(current_directory_block);
    if (path.back() == '/' && path.length() > 1)
    {                    // Check if the last character is a slash and it's not the root directory
        path.pop_back(); // Remove the last character
    }
    std::cout << path << std::endl;
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
    // 1. Convert accessrights from string to integer
    uint8_t newAccessRights;
    try
    {
        newAccessRights = static_cast<uint8_t>(std::stoi(accessrights, nullptr, 8)); // Converting octal string to integer
    }
    catch (std::invalid_argument &e)
    {
        std::cerr << "Invalid access rights format.\n";
        return -1;
    }

    // 2. Resolve the filepath
    std::vector<std::string> pathParts = resolve_path(filepath);
    if (pathParts.empty())
    {
        std::cerr << "Invalid path." << std::endl;
        return -1;
    }

    // Backup the current directory block for restoration later
    unsigned int backupCurrentDirectoryBlock = current_directory_block;

    // Find the directory of the file/directory to change permissions
    unsigned int currentBlock = current_directory_block;
    struct dir_entry *dirEntry = nullptr;
    for (size_t i = 0; i < pathParts.size() - 1; ++i)
    {
        current_directory_block = currentBlock; // Update current directory
        dirEntry = find_directory_entry(pathParts[i]);
        if (dirEntry == nullptr || dirEntry->type != TYPE_DIR)
        {
            std::cerr << "Directory not found: " << pathParts[i] << "\n";
            current_directory_block = backupCurrentDirectoryBlock;
            return -1;
        }
        currentBlock = dirEntry->first_blk; // Move to the next directory in the path
    }

    // Now, currentBlock is where the file/directory to change permissions should be
    uint8_t dir_data[BLOCK_SIZE];
    disk.read(currentBlock, dir_data);
    struct dir_entry *dir_entries = reinterpret_cast<struct dir_entry *>(dir_data);
    std::string targetName = pathParts.back(); // The last part is the name of the file/directory to change permissions
    int entryIndex = find_directory_entry(targetName, dir_entries);
    if (entryIndex == -1)
    {
        std::cerr << "Entry not found.\n";
        current_directory_block = backupCurrentDirectoryBlock;
        return -1;
    }

    // 3. Change the access rights
    dir_entries[entryIndex].access_rights = newAccessRights;

    // 4. Write back the modified directory entry to the disk
    disk.write(currentBlock, dir_data);

    current_directory_block = backupCurrentDirectoryBlock; // Restore original current directory
    return 0;
}

struct dir_entry *FS::find_directory_entry(std::string name)
{
    uint8_t current_dir_data[BLOCK_SIZE];
    disk.read(current_directory_block, current_dir_data);
    struct dir_entry *current_dir_entries = reinterpret_cast<struct dir_entry *>(current_dir_data);

    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
    {
        if (strcmp(current_dir_entries[i].file_name, name.c_str()) == 0)
        {
            return &current_dir_entries[i];
        }
    }

    return nullptr; // Return null if the name doesn't exist in the current directory.
}

int FS::find_directory_entry(const std::string &name, dir_entry *entries)
{
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
    {
        if (strcmp(entries[i].file_name, name.c_str()) == 0)
        {
            return i;
        }
    }
    return -1;
}

int FS::find_free_directory_entry(dir_entry *entries)
{
    for (int i = 0; i < (BLOCK_SIZE / sizeof(struct dir_entry)); ++i)
    {
        if (entries[i].file_name[0] == '\0')
        { // Unused entries have an empty filename
            return i;
        }
    }
    return -1; // No free entries
}

int FS::find_free_fat_entry(int start_idx)
{
    for (int i = start_idx; i < (BLOCK_SIZE / 2); ++i)
    {
        if (fat[i] == FAT_FREE)
        {
            return i;
        }
    }
    return -1; // No free FAT entries found
}

std::vector<std::string> FS::resolve_path(std::string path)
{
    std::vector<std::string> parts;
    std::stringstream path_stream(path);
    std::string part;

    while (std::getline(path_stream, part, '/'))
    {
        if (part == "" || part == ".")
            continue; // Skip 'current directory' parts
        // Don't remove ".." parts here, let the cd command handle them
        parts.push_back(part);
    }
    return parts;
}

std::vector<std::string> FS::resolve_path_for_cp_and_mv(std::string path)
{
    std::vector<std::string> parts;
    std::stringstream path_stream(path);
    std::string part;

    while (std::getline(path_stream, part, '/'))
    {
        if (part == "" || part == ".")
        {
            // skip empty or '.'
            continue;
        }
        parts.push_back(part);
    }

    return parts;
}