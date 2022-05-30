
/*
 * Implementation of the connmgr thread
 *
 * generally works fine, can get stuck in infinite loops as most error checks just discard the current message being received and continues
 */
void *connmgr_listen(void *args);

