FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    sudo openssh-server build-essential cmake gdb rsync git curl unzip \
    python3 locales tzdata && \
    apt-get clean && rm -rf /var/lib/apt/lists/*

RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8  
ENV LC_ALL=en_US.UTF-8

RUN useradd -ms /bin/bash dev && echo "dev:dev" | chpasswd && adduser dev sudo

RUN mkdir /var/run/sshd
EXPOSE 22

COPY aarch64-buildroot-linux-gnu_sdk-buildroot/ /opt/sdk/
RUN chmod +x /opt/sdk/relocate-sdk.sh && \
    /opt/sdk/relocate-sdk.sh
RUN chown -R dev:dev /opt/sdk

RUN echo '#!/bin/bash\n\
source /opt/sdk/environment-setup\n\
exec "$@"' > /entry.sh && chmod +x /entry.sh

ENTRYPOINT ["/entry.sh"]
CMD ["/usr/sbin/sshd", "-D"]