#include "openssl.h"
#include "abstraction_models.h"
#include "/playpen/cliver0/src/openssh/network_models_shared_state.h"

DEFINE_MODEL(void, sshd_exchange_identification, int sock_in, int sock_out)
{
    int i, mismatch;
    int remote_major, remote_minor;
    int major, minor;
    char *s;
    char buf[256];          /* Must not be larger than remote_version. */
    char remote_version[256];   /* Must be at least as big as buf. */
    char* tmp_server_version_string = NULL;
    ServerOptions options = *get_server_options();

    if ((options.protocol & SSH_PROTO_1) &&
        (options.protocol & SSH_PROTO_2)) {
        major = PROTOCOL_MAJOR_1;
        minor = 99;
    } else if (options.protocol & SSH_PROTO_2) {
        major = PROTOCOL_MAJOR_2;
        minor = PROTOCOL_MINOR_2;
    } else {
        major = PROTOCOL_MAJOR_1;
        minor = PROTOCOL_MINOR_1;
    }
    snprintf(buf, sizeof buf, "SSH-%d.%d-%.100s\n", major, minor,
SSH_VERSION);
    tmp_server_version_string = xstrdup(buf);

#if 0
    if (client_version_string == NULL) {
        /* Send our protocol version identification. */
#ifdef CLIVER
        if (atomicio(ktest_writesocket, sock_out, server_version_string,
strlen(server_version_string))
            != strlen(server_version_string)) {
#else
        if (atomicio(write, sock_out, server_version_string,
strlen(server_version_string))
            != strlen(server_version_string)) {
#endif
            log("Could not write ident string to %s", get_remote_ipaddr());
            fatal_cleanup();
        }

        /* Read other side's version identification. */
        memset(buf, 0, sizeof(buf));
        for (i = 0; i < sizeof(buf) - 1; i++) {
#ifdef CLIVER
            if (atomicio(ktest_readsocket, sock_in, &buf[i], 1) != 1) {
#else
            if (atomicio(read, sock_in, &buf[i], 1) != 1) {
#endif
                log("Did not receive identification string from %s",
                    get_remote_ipaddr());
                fatal_cleanup();
            }
            if (buf[i] == '\r') {
                buf[i] = 0;
                /* Kludge for F-Secure Macintosh < 1.0.2 */
                if (i == 12 &&
                    strncmp(buf, "SSH-1.5-W1.0", 12) == 0)
                    break;
                continue;
            }
            if (buf[i] == '\n') {
                buf[i] = 0;
                break;
            }
        }
        buf[sizeof(buf) - 1] = 0;
        client_version_string = xstrdup(buf);
    }

    /*
     * Check that the versions match.  In future this might accept
     * several versions and set appropriate flags to handle them.
     */
    if (sscanf(client_version_string, "SSH-%d.%d-%[^\n]\n",
        &remote_major, &remote_minor, remote_version) != 3) {
        s = "Protocol mismatch.\n";
#ifdef CLIVER
        (void) atomicio(ktest_writesocket, sock_out, s, strlen(s));
        ktest_close(sock_in);
        ktest_close(sock_out);
#else
        (void) atomicio(write, sock_out, s, strlen(s));
        close(sock_in);
        close(sock_out);
#endif

        log("Bad protocol version identification '%.100s' from %s",
            client_version_string, get_remote_ipaddr());
        fatal_cleanup();
    }

    debug("Client protocol version %d.%d; client software version %.100s",
        remote_major, remote_minor, remote_version);

    compat_datafellows(remote_version);

    if (datafellows & SSH_BUG_SCANNER) {
        log("scanned from %s with %s.  Don't panic.",
            get_remote_ipaddr(), client_version_string);
        fatal_cleanup();
    }
#endif
    //make symbolic: &remote_major, &remote_minor
    klee_make_symbolic(&remote_major, sizeof(remote_major), "");
    klee_make_symbolic(&remote_minor, sizeof(remote_minor), "");

    mismatch = 0;
    switch (remote_major) {
    case 1:
        if (remote_minor == 99) {
            if (options.protocol & SSH_PROTO_2)
                enable_compat20();
            else
                mismatch = 1;
            break;
        }
        if (!(options.protocol & SSH_PROTO_1)) {
            mismatch = 1;
            break;
        }
        if (remote_minor < 3) {
            packet_disconnect("Your ssh version is too old and "
                "is no longer supported.  Please install a newer version.");
        } else if (remote_minor == 3) {
            /* note that this disables agent-forwarding */
            enable_compat13();
        }
        break;
    case 2:
        if (options.protocol & SSH_PROTO_2) {
            enable_compat20();
            break;
        }
        /* FALLTHROUGH */
    default:
        mismatch = 1;
        break;
    }
    chop(tmp_server_version_string);
    debug("Local version string %.200s", tmp_server_version_string);
    set_server_version_string(tmp_server_version_string);

    if (mismatch) {
        s = "Protocol major versions differ.\n";
#if 0 //need to fix this, write is undeclared.
#ifdef CLIVER
        (void) atomicio(ktest_writesocket, sock_out, s, strlen(s));
        ktest_close(sock_in);
        ktest_close(sock_out);
#else
        (void) atomicio(write, sock_out, s, strlen(s));
        close(sock_in);
        close(sock_out);
#endif
        log("Protocol major versions differ for %s: %.200s vs. %.200s",
            get_remote_ipaddr(),
            server_version_string, client_version_string);
#endif
        fatal_cleanup();
    }
}


