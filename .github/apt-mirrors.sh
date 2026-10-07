#!/bin/bash
# Points apt at the Ubuntu mirrors that answer now, the others after
# them: apt asks the first mirror for every file, and one that does not
# answer costs each file a timeout. The Azure mirror stalled for hours on
# 2026-09-30; on 2026-10-07 Ubuntu's archive stopped answering, and the
# Azure mirror did for some runners.
set -u
echo 'Acquire::Retries "5"; Acquire::http::Timeout "20";' |
    sudo tee /etc/apt/apt.conf.d/80-retries >/dev/null
. /etc/os-release
answering=""
silent=""
for mirror in http://azure.archive.ubuntu.com/ubuntu/ http://mirrors.edge.kernel.org/ubuntu/ \
    http://archive.ubuntu.com/ubuntu/; do
    if curl -sfm 10 -o /dev/null "${mirror}dists/${VERSION_CODENAME}/InRelease"; then
        answering+="${mirror}"$'\n'
    else
        silent+="${mirror}"$'\n'
    fi
done
printf '%s%s' "${answering}" "${silent}" | sudo tee /etc/apt/apt-mirrors.txt
