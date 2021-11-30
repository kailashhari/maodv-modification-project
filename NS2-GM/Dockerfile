FROM ubuntu:18.04

RUN apt-get update -y

RUN apt-get install -y build-essential autoconf \
    automake libxmu-dev curl wget gcc-4.8 g++-4.8 libgtk2.0-0

RUN wget https://delta.nitt.edu/~indreshp/ns-allinone-2.35.tar.gz

RUN tar xzf ns-allinone-2.35.tar.gz

COPY install.sh .

RUN chmod +x install.sh && bash install.sh

ENV PATH $PATH:/ns-allinone-2.35/bin:/ns-allinone-2.35/tcl8.5.10/unix:/ns-allinone-2.35/tk8.5.10/unix
ENV LD_LIBRARY_PATH /ns-allinone-2.35/otcl-1.14:/ns-allinone-2.35/lib
ENV TCL_LIBRARY /ns-allinone-2.35/tcl8.5.10/library


RUN wget https://www.xgraph.org/linux/xgraph_4.38_linux64.tar.gz
RUN mkdir -p /opt/xgraph
RUN tar -xvf xgraph_4.38_linux64.tar.gz -C /opt/xgraph
RUN ln -s /opt/xgraph/XGraph4.38_linux64/bin/xgraph /usr/bin/

COPY . .

RUN chmod +x x-patch.sh && bash x-patch.sh
