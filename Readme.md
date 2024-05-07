# C-REDIS 🗄️
A Lite version of the popular in-memory database Redis implemented in C.

# Quickstart ⏩
The easiest way to get C-Redis up and running (for Unix-based) systems is to grab the binary file `redis` and execute it 
with the command: `./redis`.

If you want to build from scratch, you can build with `cmake` using the `CMakeLists.txt` file provided. If you use a 
Windows machine, I recommend building the project inside of Visual Studio.

# Features ⚙️
C-Redis implements the features present in the original version of redis including serialization and deserialization of
data from the Redis Serialization Protocol (RESP) to standard C types and support for the following commands:
1. `PING`
2. `SET` with options `EX`, `PX`, `EXAT` and `PXAT` expiry options.
3. `GET`
4. `EXISTS`
5. `DEL`
6. `INCR`
7. `DECR`
8. `LPUSH` with support for multiple arguments
9. `RPUSH` with support for multiple arguments
10. `SAVE`

C-Redis also provides support for loading a database from a `state.rdb` file provided it is in the same directory as the
binary.

# How it works
The C-Redis server is a TCP server that listens for incoming connections on the default Redis port (6379). On
startup, the server will load data from a `state.rdb` file if present and will then listen for incoming sockets on the
specified port.

Once a socket is connected (e.g. using `redis-cli`), all incoming messages must be an `RESP` message. This is the
format most redis-clients will use. C-redis will also
send out responses in the `RESP` format as well. The message type will depend on the type of response but as a rule of
thumb, errors will be sent out as `SIMPLE_ERRORS` and most other images as `SIMPLE_STRINGS`.

C-Redis uses the `poll()` function of the socket API to achieve non-blocking while waiting for data to come in. The
`poll()` function works well but can become slow when handling a giant number of connections. By using the `poll()` 
system call, we can monitor sockets and actively retire expired objects synchronously. 

Errors will also be thrown for commands not listed in the features section above.

## Storing Objects 📥
C-Redis uses a hashmap from the `UTHash` library to store and retrieve objects. You can find information about UThash 
[here](https://troydhanson.github.io/uthash/userguide.html). Storing objects is a guaranteed O(1) operation.
Each key-value pair is stored in the structure:
```C
typedef struct {
    char *key;
    char *value;
    unsigned long exp_milliseconds;
    int expire_list_index; // -1 if expiry was never set.
    int array_size; // 0 for non-arrays, number of items for arrays
    UT_hash_handle hh; /* makes this structure hashable */
} redis_object;
```
All data types as stored as strings (character arrays). Actual arrays are stored as
delimited strings with the  (`^`) character as the delimiter with the `array_size` field set to a positive integer N.

## Retrieving Objects 📤
Getting/retrieving objects is also guaranteed to be an O(1) operation. If the object exists,
it is returned as a `SIMPLE_STRING` and if not, a `SIMPLE_ERROR` is returned.

## Checking Existence 📬
Checking if an object exists is also an O(N) operation where N is the number of keys supplied. C-Redis will return a 
`SIMPLE_INTEGER` denoting the number of keys that were found to exist.

## Deleting Objects 🗑️
This also an O(N) operation where N is the number of keys to be deleted. Just like with `EXISTS`, C-Redis will also
return a `SIMPLE_INTEGER` denoting the number of keys deleted.

## Incrementing and Decrementing ➕➖
Incrementing or decrementing values are both O(1) operations. Since C-Redis stores all values as strings, an error will 
be returned if the value to be incremented or decremented cannot be represented as an integer. Otherwise, the value 
incremented or decremented by 1 will be returned.

## Storing Lists 📋
Lists of values can be stored in C-Redis using the `LPUSH` or `RPUSH` commands. This operation is an O(N) operation 
where N is the number of arguments supplied after the key. `LPUSH` will append elements to the head of the list while 
`RPUSH` appends them to the tail. If the key already exists and is not a list, an error will be returned. Otherwise, the
number of elements in the list will be returned.

## Expiring Objects ⏳
Just like the original redis, C-Redis implements two methods for deleting expired objects.

The first method is when a `GET` command is received. C-Redis checks to see if the object has an expiration set and if 
yes, determines if the object is expired. If it is, then the object will be deleted and a `SIMPLE_ERROR` will be returned
denoting the absence of the key. This method is called `Passive Expiration`.

The second method periodically checks to determine which objects have an expiration time set and have expired. This is
achieved by  storing all expiration enabled objects in an array with each object holding a reference to it's position in 
the array. Once an objects has been expired, the last element in the expiration array is moved to that index and the
index of the moved element is updated. This way, we can guarantee that expiring an object is a guaranteed O(1) operation.
This method is called `Active Expiration`.

## SAVE 💾
The save operation is an O(N) operation. C-Redis saves the entire state of the database into a plain `state.rdb` file 
that can be reloaded on startup.

# Benchmark 🏋️
The `redis-benchmark` tool was used to test C-redis against actual redis on a linux box with 8GB RAM. Here's how it 
performed:
```
SET: 22727.27 requests per second
GET: 25608.20 requests per second
```
For context, here's how the full-blown redis performed on the same machine:
```
SET: 73475.39 requests per second
GET: 72411.30 requests per second
```

# Current Limitations ⚠️
C-Redis has been implemented to be a lightweight version of the original redis. Here are some of its limits:
1. Can only handle inputs commands containing up to 1024 characters.
2. Can hold a maximum 4096 objects.
3. Can handle 10 concurrent connections.

# Future Improvements ⚙️
1. Improve the data structures used for storing objects
2. Handle concurrent access more efficiently.

# References 📖
1. [Redis documentation](https://redis.io/docs/latest/)
2. [Beej's Guide to Network Progamming](https://beej.us/guide/bgnet/html/)
3. [Build Your Own Redis](https://codingchallenges.fyi/challenges/challenge-redis/)