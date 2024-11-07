# Disk Imaging Library
Using the FUSE library, we created a simple disk imaging library for CS 5600 that Linux OS machines can use. This incorporated low-level programming which mapped disk space where the disk imaging library would traverse it. 

## Implementations
We have implemented the following features: 
 - `get_attr`: retrieves the attribute of path
 - `readdir`: reads the directory of path
 - `read`: reads the file from the path
 - `utimens`: updates the timestamps of files
 - `create`: creates a file in the disk
 - `unlink`: removes a file from the disk
 - `mkdir`: makes a directory
 - `rmdir`: removes a directory if it is empty
 - `rename`: renames a file or directory given a path
 - `write`: writes to a file
 - `chmod`: changes access permissions of a path
 - `truncate`: resizes a file to 0 bytes
