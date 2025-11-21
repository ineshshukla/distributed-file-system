1. "The goal is a Christmas launch, so timely delivery is critical for this MVP (no deadline extensions).".... This is written in the Introduction section of the Course project doc. Does this mean we cannot use late days?
**[AG]** - No, you can use late days; in accordance with the usual course policies, as shared before.
 
2. Say one client is editing a particular file while someone else is streaming the same file. How shall it output?
**[AG]** - As long as the WRITE is not completed (the client has sent ETIRW), the file content remains the original content. So, STREAM should display the original file content.

3. [10] Get Additional Information: Users can access a wealth of supplementary information about specific files. This includes details such as file size, access rights, timestamps, and other metadata, providing users with comprehensive insights into the files they interact with.
What does timestamps mean in this case?
**[AG]** - Time of file creation, last edited time and anything more you would like to add.

4. In example 4: write to a file: Client: WRITE mouse.txt 2 # Inserting a sentence delimiter Client: 5 and AAD. aaaah # New sentences : [I dont like T-T PNS and AAD.]* [aaaah]. Currently active status remains with the index at index 2 Client: 0 But, # New sentence : [But, I dont like T-T PNS and AAD.]* [aaaah]. Client: ETIRW Write Successful!, shouldn't the sentence number be 1 and not 2 in the WRITE command? (because up until the previous line, we havent added a delimiter to sentence 1) and the active status also remains at sentence number 1 right?
**[AG]** - Ah yes, true. It should be 1 only (mb)

5. In Example 10: Access Control: Client: ADDACCESS -W nuh_uh.txt user3 Access granted successfully! --> File: feedback.txt --> Owner: user1 --> Created: 2025-10-10 14:21 --> Last Modified: 2025-10-10 14:32 --> Size: 52 bytes --> Access: user1 (RW), user2 (RW) --> Last Accessed: 2025-10-10 14:32 by user1, isn't "File:" supposed to display the file's name (which is nuh_uh.txt)? and in "Access:" why isn't user3 added (assuming there exists user3 coz the message says "Access granted successfully")?
**[AG]** - Yes yes user3 is a typo, it was meant to be user2 only. Will fix, thnx for pointing out!

6. "After each sentence write update, the index must word_index must update for the next sentence." Could you please explain what this sentence means
**[AG]** - Fixed the wording, pls go through that. Also, an example issue (with possible solution) was shared in the 18th Oct tutorial. So, refer to that for more clarity.

7. Just to confirm, -W flag would provide BOTH read and write access to the user right?
**[AG]** - Yes

8. Referring to the word-index in example 4. Should we take it as 0-indexed or 1-indexed? And is the index referring to a particular word or the positions between the words? For example, say a sentence has "A B C D E" will "1 Z" result in "A Z B C D E" and 0 Z result in "Z A B C D E"? There are 4 cases of inserting a word using word index in example 4, sentence 0 word 4 and word 6, and sentence 1 word 5 and word 0, and the policy for inserting words doesn't match in all the cases.
**[AG]** - The example assumes 0-index, but feel free to choose whatever suits you. While there shouldn't be any disparity in the provided examples (I'll reconfirm and fix, if any), but the indexing and index-values can be chosen by you in whatever system you prefer, as long as the underlying structure for WRITE calls is preserved.

9. Under Bonus Functionalities, in Hierarchical Folder Structure, is the structure created by a user expected to be persistent? i.e. when the user logs back in, he should start off with the folder structure he left with?
**[AG]** - Not necessarily, you can have them starting from the root folder.

10. Under Bonus Functionalities, do checkpoints need to be persistent? Also, are checkpoints file specific or user specific?
**[AG]** - Yes, that's the whole point of checkpoints: To be able to revert to them from anytime in the future. They are file specific.

11. It is mentioned that Reading, Writing, Streaming : The NM identifies the correct Storage Server and returns the precise IP address and client port for that SS to the client. Subsequently, the client directly communicates with the designated SS. How exactly does NM identifies the correct SS for the client (in case there are multiple SS hosting the same file)?
**[AG]** - Any of the SS containing that file would be the "correct SS". So, NM can return the IP and port of any of them, preferrably one with lower load if you can employ some mechanism to judge that.

12. Lets consider File1 is in SS1 and File2 is in SS2. Say direct connection between SS1 and client is established. Now if client wants to access File2 will a new connection establish between SS2 and client or will it just say file not exists?
**[AG]** Client-SS relations are per request. So, after the client's File1 request is finished the Client-SS1 connection is terminated. For the next request to File2 from SS2 (or even File1 from SS1), a fresh connection between the two need to be established.

13. What exactly does it mean by sending a predefined STOP packet? Does it mean that the user has to send a STOP packet explicitly, or after read, write or stream task is completed, it automatically terminates the connection?
**[AG]** - A STOP packet signifies the end of the communication. It must be explicitly sent for the receiver to realise the end of data / communication.

14. Do we have to define each data blocks size and the number of data blocks in the storage? Also, is there any restriction on the storage capacity of each storage server?
**[AG]** - Depending on your implementation, you might have to. There is no specific restriction, as in your system storage is the maximum storage capacity of the server. But, you can expect the evaluation requirements to not go beyond a few MBs of data.

15. In example 4,
    ```
    Client: READ mouse.txt
    Im just a deeply mistaken hollow pocket-sized lil gei-fwen mouse. I dont like T-T PNS

    Client: WRITE mouse.txt 1  # Inserting a sentence delimiter
    Client: 5 and AAD. aaaah # New sentences : [I dont like T-T PNS and AAD.]* [aaaah]. Currently active status remains with the index at index 1
    Client: 0 But,  # New sentence : [But, I dont like T-T PNS and AAD.]* [aaaah].
    Client: ETIRW
    Write Successful!

    Client: READ mouse.txt
    Im just a deeply mistaken hollow pocket-sized lil gei-fwen mouse. But, I dont like T-T PNS and AAD. aaaah.
    ```
    Why has there been a delimiter added at the end of `aaaah`?

**[AG]** - For a user, it is just a full stop, they can add it anywhere. The system interprets it as a delimiter. So, no reasoning as to why the user chose to end his statement with a full-stop.

16. Should we include LLM Generations with the files?
**[AG]** - Yes, just clearly demarkate it, and add appropriate links in a seperate README file.

17. ![image](https://hackmd.io/_uploads/SyaIz_Ey-l.png)
       this was shown in the tut online explanation, just wanted to make it clear
       does it work like, lets say it was
       [S1->hello->folks
       S2-> hows->life
       so on ..]
       I want to insert the following after hello
       S->hi
       S'->!
       S''->!
       so will the final be
       [
       S1-> hello hi
                   ! 
                   ! folks         
       ]
       is this correct?
**[AG]** - Yes (it's just a demo implementation, you may choose to implement different data structure, etc)

18. does backup start automatically when we connect one of the storage server, or does that also require explicit connection. for eg: if I start storage server 1, does the backup for it start automatically, or I need to start its backup also myself?
**[AG]** - As soon as a new 'empty' server comes up online, it can start working as a backup for some other server. The decision wether it acts as a backup or not, depends on your implementation. But you should ensure that if resources are available, all data has at least one copy for backup.

19. For READ/WRITE/STREAM operations: After NM gives SS details to client, should the client open a new TCP socket to SS? Should SS have two listening ports: one for NM, one for clients?
**[AG]** - Yes, while maintaining its connection to NM. Yes

20. How exactly is a user trying to write affected when there are >=1 readers or streamers? For example, if the user is trying to commit his write while the file is being read/streamed. Should this request be queued after the reads/streams? Or should the writer be given priority by interrupting the readers/streamers?
**[AG]** - (Answered previously) Unless the WRITE is completed, all accesses to the file return the original data (before write). Priority should be given to read/stream.

21. Till what extent are we expected to handle packet loss? Can we assume no "ACK"s will be lost or are we supposed to handle loss of ACKs as well? Please clarify the extent of error handling for the networking side of things.
**[AG]** - The user should never exit abruptly. If there is a loss of packet, there should be mechanisms for retransmission and fall-backs. Handle worst-case scenarios of whole network chain broken, and appropriate and graceful error handling. Some retransmission is expected.

22. The 5 marks of bonus will be granted based on how we implement the project specifications or will they also be granted based on new/extra features we devise?
**[AG]** - Assuming this doubt is for the "Unique Factor" part. New / extra feature.



23. Undo Change: Users can rever the last changes made to a file. does this mean that if i do WRITE <some_sentences> and then i do ETIRW and after that  when i undo it removes ALL the sentences that were written by WRITE? Also if i do undo again, should it restore the sentences or undo whatever change was made before that or do nothing?
**[AG]** - It undoes all the sentences within that single WRITE command. Undo again, is not redo. It will undo whatever change was made before that.

24. `Client: WRITE mouse.txt 1  # Inserting a sentence delimiter
Client: 5 and AAD. aaaah # New sentences : [I dont like T-T PNS and AAD.]* [aaaah]. Currently active status remains with the index at index 1
Client: 0 But,  # New sentence : [But, I dont like T-T PNS and AAD.]* [aaaah].` Here, if I try the command : `Client: 9 abcd` Should I get an error because I'm referencing next sentence or should it succeed?
**[AG]** - 9 would give error. But 8 would append to the first sentence. You cannot jump sentences by giving a large index. 
    
25. The SS sends a list of files on it when it registers. Can we assume that 4096 bytes is enough for a very long list?
**[AG]** - No, we'll prefer a more robust way, like a stream of packets you send until the whole list is sent with an associated ACK bit of sorts to confirm the whole list is transmitted.

26. Should we Load ALL files at server startup (slower startup, all files always ready)or do Lazy loading - load file on first access, keep in memory once loaded (faster startup) what to do?
**[AG]** - No, you should never load all data files. Bad design when you have humongous data stores. You should always load data on request ONLY. And ensure you keep clearing memory (cache) after some time. The whole point of cache is to be small for faster access.

27. Are the shell commands in the file to be EXEC'd singular commands? Or can they include pipes, background &s etc?
**[AG]** - Won't be singular commands, can include file read/writes, some computation, etc. (Ideally, you should not run such files on servers as is, very insecure). But, you can assume that these files would clear up all the data / files they create, not mess up any existing data and would not hog resources.

28. If access is attempted to be given to a user that is not registered (does not show up in list), should we give an error?
**[AG]** - Yes, an unregistered user should ideally not even get access to the interface.
    
29. Is cjson header allowed to be used?
**[AG]** - You may use any POSIX library.
    
30. If a library is not posix, is it allowed to be used?
**[AG]** - No, only POSIX libraries are allowed.
    
31. Following up on 27., they can be commands other than bash also? Please clarify 'shell commands'. They need not be bash commands? So they can be READ, WRITE, STREAM, CREATEFOLDER etc.?
**[AG]** - Only bash commands would be there
    
32. For the EXEC operation, do we have to separately interpret the file and then send for execution? For example, let a series of WRITE operations lead to the file `hi.txt` have `echo hiii. ls`. Is the expected output:
        `hiii
        \n<list of files in directory>`
or      `hiii.
        \n<list of files in directory>`
or      `hiii. ls`
**[AG]** - Treat the whole file as a bash script.

33. For EXEC operation, are we supposed to send request from client to ns, then fetch the file from ss and execute them on ns itself? So this flow has to be different from the normal READ and WRITE operations right? And should the whole file be treated as a bash script? Or we have to parse it sentence by sentence?
**[AG]** - Yes, the flow would be different from READ / WRITE. Yes, treat it as a bash script, dont parse it sentence-by-sentence.
    
34. Is the undo command supposed to store only one level of history, or is the code supposed to support multiple simultaneous undos chained together?
**[AG]** - Just one-level works, we only need a proof of concept

35. When we are implementing WRITE, do we add a newline after adding each sentence? According to example 2, yes. According to example 4, no. So what do we do?
**[AG]** - The content of the file would have '\n' to signify new line. Both examples follow that, just example 4 never encountered a '\n'.
    
36. I have 2 clients alice and bob, and then quit the client alice in the alice server, and then run LIST from the bob server. Should the output be:
    -->alice
    -->bob
since alice was technically registered before? or does the output change to:
    -->bob
**[AG]** - Registered users include users not currently online. You must store all users that have logged in till date.
    
37. In the document it is mentioned "At any point, there would be a single instance of the Name Server running, to which multiple instances of Storage Servers and User Clients can connect. The User Clients and Storage Servers can disconnect and reconnect at any time, and the system should handle these events gracefully." My question - Does NM need to be able to disconnect using ctrl+c? or is stopping it using ctrl+z fine, as the exiting of this server is not mentioned in the project document?
**[AG]** - No, you can assume that once NM starts, it stays on for the whole duration. NM going down is not in the scope of this project.

38. If the owner is attempting to delete a file, but the file is being written to by another user, are we expected to:
    a) Prevent the owner from deleting with an error message?
    b) Give an error message to the editing user?
**[AG]** - Prevent deletion as the sentence is locked.
    
39. In the 23rd answer you have mentioned that multiple undoes are supported for one file while the project doc explicitly states they are not(under example 5)?!
**[AG]** - The minimum requirement is one-level undo. People might choose to expand features or develop their unique things for the bonus.
    
40. For performing UNDO, does a user need to have write permission for that file?
**[AG]** - Yes

41. What is expected to be done in caching? Does it need to be implemented on the ns or ss? As on the ns, it would just involve caching the storage server id for the corresponding file, whose access is already O(1) or O(log n) average case.
**[AG]** - It won't be O(1), might tend to but not O(1). But the cached mappings should be O(1) always.

42. can we use glibc?
**[AG]** - You may use any (and only) POSIC libraries.

43. do access lookups also need to be fast? 
**[AG]** - Yes, they need to be sub-linear time complexity. And cached ones need to be constant time.

44. If a user is writing to a file and has a sentence locked, but the client disconnects midway, should other users be allowed to write to that sentence? Do we leave the sentence as locked until the previously disconnected user writes that sentence again?
**[AG]** - If the client disconnects midway a WRITE (ie, before sending the ERITW), no changes of that write should pass. It's as if the write never happened. And all locks are relieved for other users to access the file without any issues.

 
45. For running the EXEC command, Example 11 in the document shows commands being separated by newline characters. Is that how the file should be in our document as well? Or separated based on sentence delimiters, as that is how sentences are handled in the writing of files?
For example:
    "ls
    echo "Hello"
    
    or "ls. echo "Hello".
**[AG]** - The file contents, as is, should be treated as a normal bash script for running. The delimiters don't play any role.
    
46. Can we assume that WRITE to a file will always happen via terminal ONLY and not directly to the file? If only through terminal, then how to add newline characters to the file?
**[AG]** - Yes, it can only happen to terminal. Something like '\n' should work, depends on how you want to implement. Open for you to decide.
    
47. For blocking operations like EXEC on the Name Server and WRITE on the Storage Server, is it acceptable to let the main epoll thread block, or are we required to implement a thread pool to handle them asynchronously?
**[AG]** - Thread pool would be preferred

48. only file-lookups need to be efficient?
**[AG]** - Can you elaborate on what you mean by 'only'...

49. 
```
Client: READ mouse.txt
Im just a mouse. I dont like T-T PNS
Client: WRITE mouse.txt 0  # Inserting multiple times into a sentence
Client: 4 deeply mistaken hollow lil gei-fwen # New sentence : Im just a deeply mistaken hollow lil gei-fwen pocket-sized mouse.
Client: 6 pocket-sized  # New sentence : Im just a deeply mistaken hollow pocket-sized lil gei-fwen mouse.
Client: ETIRW
Write Successful!

Client: READ mouse.txt
Im just a deeply mistaken hollow pocket-sized lil gei-fwen mouse. I dont like T-T PNS
```

The words for sentence(0) are : (0) Im (1) just (2) a (3) mouse. I dont like T-T PNS
shouldn't the second line of input be "Client: 3 deeply mistaken hollow lil gei-fwen" instead of client: 4 ?
**[AG]** - Yes, some oopsie-woopsie from the TAs side while handling 0-based / 1-based indexing.
    
50. in q43, we are expected to implement caching for access lookups as well???
**[AG]** - Yes, file access (I assume you mean the SS mappings, from this), need to have some cache implementation.
    
51. lets say a client 1 is writing to a file at sentence index 1 so logically other client 2 must give sentence is locked for writing at index 1, but should it give the same for index 2 or it should be able to write at any other index except for 1. also can client 2 read the file while client 1 is writing.
**[AG]** - Explained in detail in the tutorial video. Refer to that
    
52. Suppose client 2 is in a WRITE session to a file whose owner is client 1. While client 2 is writing, client 1 removes the file access to client 2. Should that write session succeed or not?
**[AG]** - You can make a justifiable asumption, and work with that. 

53. lets say:
    WRITE file.txt 0
    1 hello
    2 ww
    ETIRW
    
    WRITE file.txt 0
    1 finally !
    ETIRW
    
    WRITE file.txt 1
    1 demn
    ETIRW
    
    what should the output of this be?
    is it-  finally hello ww! demn
    or is it- finally! demn hello ww
    pls confirm asap
    
54. For STREAM, is it fine, if there is no gap between the words printed?
    
    