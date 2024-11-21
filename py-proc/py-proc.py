import os

def get_proc() -> dict:
    directory = "/proc"
    files = [f for f in os.listdir(directory) if f.isdecimal()]
    pids = {}
    for pid in files:
        pid_path = os.path.join(directory, pid)

        pid_info = dict()
        with open(os.path.join(pid_path, "status")) as f:
            for l in f.readlines():
                entry = l.split(":")
                pid_info[entry[0]] = entry[1].strip()
            
        # print(status)

        pid_info["fd_count"] = len(os.listdir(os.path.join(pid_path, "fd")))

        inodes = dict()  # {inode: file_descripter}
        for fd_name in os.listdir(os.path.join(pid_path, "fd")):
            fd_path = os.path.join(pid_path, "fd", fd_name)
            link = os.readlink(fd_path)
            if 'socket' in link:
                inode = link.split('[')[1].rstrip(']')
                # print(f"FD {fd_name}: {inode}")
                inodes[inode] = fd_name
        pid_info["inodes"] = inodes

        with open(os.path.join(pid_path, "cmdline")) as f:
            pid_info["cmdline"] = f.read()
        
        pids[pid] = pid_info
    
    return pids


def netstat(pids):
    print("netstat")
    directory = "/proc/net"
    targets = ["tcp", "udp", "tcp6", "udp6", "raw", "raw6"]

    lines = []
    for tar in targets:
        with open(os.path.join(directory, tar)) as f:
            lines += f.readlines()[1:]
    
    # print(lines)
    inode_pos = 7
    for l in lines:
        # print(l)
        spl = l.split()
        inode = spl[inode_pos]
        print(inode)
        for pid in pids.keys():
            # print(f'\t{pids[pid]["inodes"]}')
            if inode in pids[pid]["inodes"]:
                print(f"inode {inode} matches {pid}")



def print_pids(pids):
    # print(tabulate.tabulate(pids, tablefmt="grid"))
    print("pid\tppid\tname\t\topen_fd\tcmd")
    for p in pids.values():
        print(f'{p["Pid"]}\t{p["PPid"]}\t{p["Name"]}\t\t{p["fd_count"]}\t{p["cmdline"]}')
        # print("\tinodes: ", p["inodes"])
        

def main():
    pids = get_proc()
    # print_pids(pids)
    netstat(pids)


if __name__ == "__main__":
    main()
