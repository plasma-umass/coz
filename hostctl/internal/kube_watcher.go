package internal

import (
    "encoding/json"
    "fmt"
    "os/exec"
    "strings"
)

// ResolvePodCgroup attempts to map namespace/pod → cgroup path quickly
// by invoking `kubectl get pod -o json` (works inside a node with kubectl).
// In production replace with direct CRI / kubelet API.
func ResolvePodCgroup(ns, pod string) (string, error) {
    out, err := exec.Command("kubectl", "get", "pod", "-n", ns, pod, "-o", "json").CombinedOutput()
    if err != nil {
        return "", fmt.Errorf("kubectl: %v", err)
    }
    var obj struct {
        Metadata struct{ UID string `json:"uid"` } `json:"metadata"`
    }
    if err := json.Unmarshal(out, &obj); err != nil {
        return "", err
    }
    if obj.Metadata.UID == "" {
        return "", fmt.Errorf("uid not found")
    }
    cg := filepath.Join("/sys/fs/cgroup/kubepods.slice", "pod"+string(obj.Metadata.UID))
    return cg, nil
}