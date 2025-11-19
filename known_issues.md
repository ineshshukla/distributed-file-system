- addaccess doesnt seem to be working at all - it worked when i created a new user, a new file, adn then added access to an old user, but not when i did it the other way around
- `-RW` doesnt seem to be implemented I think, which can be okay but success message should definitely not be given for it
- addaccess to unknown user is also being allowed(which btw works once the user is created)-just a poor design choice
- In addaccess, when username not given -still shows `file '' not found` error, instead of `username '' not found`  
- `-W` gives read access as well (also shows current sentence when writing(sortof read)-which can be a design choice)
  
- dynamically update size of table formed at `view -al` based on largest filename
- add `help` functionality for all commands and usage (interface)
- `view c.txt` works when it should give error for wrong usage of command(gives `view` output) - parse issue

- write issue: weird output for this: empty file before this command run
```
> write x.txt 0
WRITE session ready for 'x.txt' (sentence 0)
Enter '<word_index> <content>' lines (0-based indices). Type ETIRW to finish.
WRITE> 0 haha boy
edit applied
WRITE> 1 raunak   
edit applied
WRITE> 1 toy , you.
edit applied
WRITE> 4 hehe baba
edit applied
WRITE> 120 hehee
ERROR [INVALID]: Word index out of range
WRITE> ETIRW
Write Successful!
> view x.txt
No files found. (Use -a to view all files)

> read x.txt
haha toy , you. hehe baba raunak boy

```

### unanswered questions:
- is write asynchronous: like nm can handle write, while user is allowed to do other work?
