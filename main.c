#include "redis.h"

int main() {
    // Tests:
    // 1. Serialize, deserialize and print an RESP Integer - Done
    // 2. Serialize, deserialize and print an RESP simple string - Done
    // 3. Serialize, deserialize and print an RESP bulk string - Done
    // 4. Serialize, deserialize and print an RESP bulk array - Done

    // 5. Serialize, deserialize error - Done
    // 6. Listen via TCP on port 6379, handle multiple clients at once via non-blocking means - Done
    // 7. Receive valid CMDs and send valid RES - Done
    // 8. Refactor code - Done
    // 9. Improve error checking
    //

    redis_server_listen();
    return 0;
}
