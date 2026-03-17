__attribute__((visibility("default"))) void linbox_noop(void) {
    /* Intentionally empty scaffold symbol. */
}

__attribute__((constructor)) static void linbox_ctor(void) {
    /* Keep constructor side-effect free for scaffold stage. */
}
