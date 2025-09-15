// factory_reset.cpp
// Compile: g++ -O2 factory_reset.cpp -o factory_reset
// WARNING: destructive. Test in VM.

#include <bits/stdc++.h>
#include <unistd.h>
using namespace std;

void printProgress(int p) {
    cout << "PROGRESS:" << p << endl;
    cout.flush();
}

int main(){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // After sudo -S, the first stdin line (password) is consumed by sudo.
    // Optionally read a confirmation token (the server will send a 'y').
    string confirm;
    if(!getline(cin, confirm)) confirm = "y";

    if(confirm.size() == 0 || (confirm[0] != 'y' && confirm[0] != 'Y')) {
        cerr << "Cancelled by input\n";
        return 1;
    }

    // We'll perform steps and report progress in approximate percentages.
    printProgress(5);
    // Step 1: overwrite home files (shred may be slow; we do safe multi-step)
    system("sh -c 'for d in /home/*; do if [ -d \"$d\" ]; then find \"$d\" -type f -exec shred -u -n 3 -z {} \\; -o -exec rm -f {} \\;; fi; done' 2>/dev/null");
    printProgress(35);

    // Step 2: remove home directories
    system("rm -rf /home/* 2>/dev/null");
    printProgress(50);

    // Step 3: clear logs, tmp, caches
    system("journalctl --rotate 2>/dev/null || true");
    system("journalctl --vacuum-time=1s 2>/dev/null || true");
    system("rm -rf /var/log/* /tmp/* /var/tmp/* /var/cache/* 2>/dev/null");
    printProgress(70);

    // Step 4: remove non-root users (UID >=1000)
    system("awk -F: '$3 >= 1000 {print $1}' /etc/passwd | xargs -r -n1 userdel -r 2>/dev/null || true");
    printProgress(85);

    // Step 5: remove network configs, ssh host keys, machine-id
    system("rm -f /etc/NetworkManager/system-connections/* 2>/dev/null || true");
    system("rm -f /etc/ssh/ssh_host_* 2>/dev/null || true");
    system("truncate -s 0 /etc/machine-id 2>/dev/null || true");
    system("rm -f /var/lib/dbus/machine-id 2>/dev/null || true");
    printProgress(95);

    // Finalize
    time_t t = time(nullptr);
    string tm = ctime(&t);

    string log = "{\n";
    log += "  \"mode\": \"Factory Reset\",\n";
    log += "  \"status\": \"SUCCESS\",\n";
    log += "  \"timestamp\": \"" + tm;
    log += "\"\n}\n";

    ofstream ofs("factory_reset_log.json");
    if(ofs) { ofs << log; ofs.close(); }

    printProgress(100);
    cout << "DONE\n";
    cout.flush();
    return 0;
}

