#include <iostream>
#include "fs.h"

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
    std::memset(fat, FAT_FREE, sizeof(fat));

    // Mark block 0 (the root directory) as used
    fat[0] = FAT_USED;

    // Mark block 1 (the FAT block) as used
    fat[1] = FAT_USED;

    // Update the FAT on the disk
    if (disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat)) != 0) {
        std::cerr << "Error: Failed to update the FAT on the disk\n";
        return -1;
    }

    // Mark all other blocks as free
    for (unsigned i = DATA_BLOCK_START; i < NO_BLOCKS; ++i) {
        fat[i] = FAT_FREE;
    }

    // Update the FAT on the disk
    if (disk.write(FAT_BLOCK, reinterpret_cast<uint8_t*>(fat)) != 0) {
        std::cerr << "Error: Failed to update the FAT on the disk\n";
        return -1;
    }

    std::cout << "Disk formatted successfully.\n";

    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
    std::cout << "FS::create(" << filepath << ")\n";

    // Open the file for writing
    std::ofstream file(filepath, std::ios::binary | std::ios::out);
    if (!file.is_open()) {
        std::cerr << "Error: Failed to create file " << filepath << "\n";
        return -1;
    }

    std::string line;
    while (true) {
        // Read a line of input
        std::getline(std::cin, line);

        // Write the line to the file
        file << line << std::endl;

        // Check if the line is empty, indicating the end of input
        if (line.empty()) {
            break;
        }
    }

    // Close the file
    file.close();

    std::cout << "File " << filepath << " created successfully.\n";

    return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
    std::cout << "FS::cat(" << filepath << ")\n";

    // Open the file for reading
    std::ifstream file(filepath, std::ios::binary | std::ios::in);
    if (!file.is_open()) {
        std::cerr << "Error: Failed to open file " << filepath << "\n";
        return -1;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << std::endl;
    }

    // Close the file
    file.close();

    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int FS::ls()
{
    std::cout << "FS::ls()\n";

    // Open the current directory (root directory)
    DIR* dir = opendir(".");
    if (dir == nullptr) {
        std::cerr << "Error: Failed to open current directory\n";
        return -1;
    }

    // Read the directory entries
    dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;

        // Skip "." and ".." entries
        if (filename == "." || filename == "..") {
            continue;
        }

        // Print the file name
        std::cout << filename;

        // Get the file size
        std::string filepath = "./" + filename;
        FILE* file = fopen(filepath.c_str(), "r");
        if (file != nullptr) {
            fseek(file, 0, SEEK_END);
            long filesize = ftell(file);
            fclose(file);
            std::cout << " " << filesize;
        }

        std::cout << std::endl;
    }

    // Close the directory
    closedir(dir);

    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";

    // Open the source file for reading
    int sourceFile = open(sourcepath.c_str(), O_RDONLY);
    if (sourceFile == -1) {
        std::cerr << "Error: Failed to open source file " << sourcepath << "\n";
        return -1;
    }

    // Open the destination file for writing
    int destFile = open(destpath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (destFile == -1) {
        std::cerr << "Error: Failed to open destination file " << destpath << "\n";
        close(sourceFile);
        return -1;
    }

    // Copy the contents of the source file to the destination file
    char buffer[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(sourceFile, buffer, sizeof(buffer))) > 0) {
        if (write(destFile, buffer, bytesRead) != bytesRead) {
            std::cerr << "Error: Failed to write to destination file\n";
            close(sourceFile);
            close(destFile);
            return -1;
        }
    }

    // Close the files
    close(sourceFile);
    close(destFile);

    std::cout << "File " << sourcepath << " copied to " << destpath << " successfully.\n";

    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";

    // Check if the source file exists
    if (access(sourcepath.c_str(), F_OK) == -1) {
        std::cerr << "Error: Source file " << sourcepath << " does not exist\n";
        return -1;
    }

    // Check if the destination file already exists
    if (access(destpath.c_str(), F_OK) == 0) {
        std::cerr << "Error: Destination file " << destpath << " already exists\n";
        return -1;
    }

    // Rename the file
    if (rename(sourcepath.c_str(), destpath.c_str()) == 0) {
        std::cout << "File " << sourcepath << " renamed to " << destpath << " successfully\n";
        return 0;
    } else {
        std::cerr << "Error: Failed to rename file " << sourcepath << "\n";
        return -1;
    }
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";

    // Check if the file exists
    if (access(filepath.c_str(), F_OK) == -1) {
        std::cerr << "Error: File " << filepath << " does not exist\n";
        return -1;
    }

    // Remove the file
    if (remove(filepath.c_str()) == 0) {
        std::cout << "File " << filepath << " deleted successfully\n";
        return 0;
    } else {
        std::cerr << "Error: Failed to delete file " << filepath << "\n";
        return -1;
    }
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";

    // Open the source file for reading
    std::ifstream sourceFile(filepath1, std::ios::binary);
    if (!sourceFile) {
        std::cerr << "Error: Failed to open source file " << filepath1 << "\n";
        return -1;
    }

    // Open the destination file for appending
    std::ofstream destFile(filepath2, std::ios::binary | std::ios::app);
    if (!destFile) {
        std::cerr << "Error: Failed to open destination file " << filepath2 << "\n";
        sourceFile.close();
        return -1;
    }

    // Copy the contents of the source file to the destination file
    destFile << sourceFile.rdbuf();

    // Close the files
    sourceFile.close();
    destFile.close();

    std::cout << "Contents of " << filepath1 << " appended to " << filepath2 << " successfully.\n";

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
