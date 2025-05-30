func handleDelay(usec int) {
    for _, cg := range otherCgroups {
        os.WriteFile(filepath.Join(cg, "cgroup.freeze"), []byte{'1'}, 0644)
    }
    time.Sleep(time.Duration(usec) * time.Microsecond)
    for _, cg := range otherCgroups {
        os.WriteFile(filepath.Join(cg, "cgroup.freeze"), []byte{'0'}, 0644)
    }
}