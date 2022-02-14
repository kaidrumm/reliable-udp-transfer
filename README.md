Author: Kai Drumm
CSCI5273 Spring 2022
Programming Assignment 1: UDP Socket Programming

This program implements reliable file transfer between a client and server using unconnected UDP (datagram) sockets.

Usage:
make
./server <port number above 5000>
./client <ip address of server> <matching port number>

Run the two programs on different machines. Identify the server's IP address using the command "hostname -I". 

In the client, you can type:
- get [file_name]
- put [file_name]
- delete [file_name]
- ls
- exit

Starting files should be stored in the client/files/ folder. All files received by the server are saved to server/files/. Files received by the client with a "get" command are saved to client/files/received/.

The 'ls' command only lists files in the server/files/ folder, excluding hidden files that start with a dot.
The 'exit' command only causes the server to exit. The client will remain running.
The 'delete' command only deletes files on the server, not the client.


This program has been tested on files up to 285 MB in size. It uses packet IDs which go up to a maximum of
4,294,967,293 (the last two are reserved for flags), and each packet can hold 1020 bytes, so the maximum file size it can transfer might be 4.38 terabytes.
I don't recommend sending such a large file because my program is slow. 
You could remove some of the progress printouts to make it run faster.
Also, the packet contents are written to file via fseek() calls that move to various locations within the file, 
and I don't know if this would break with larger files that don't fit into memory.

This code is my own work. Credit:  I copied the macros for bit setting and testing from an Emory CS class website 
(http://www.mathcs.emory.edu/~cheung/Courses/255/Syllabus/1-C-intro/bit-array.html)
and some boilerplate code from Beej's guide (http://beej.us/guide/bgnet/pdf/bgnet_usl_c_2.pdf).
This program has not been optimized for speed, readability, memory leaks, code organization, or aesthetic user interface; 
but the files sent and received have matching Md5 hashes.
Sorry for the disorganized code!