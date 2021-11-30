cp maodv/aodv* /ns-allinone-2.35/ns-2.35/aodv/
cp maodv/cmu-trace.cc /ns-allinone-2.35/ns-2.35/trace/
cp maodv/node* /ns-allinone-2.35/ns-2.35/common/
cp maodv/wireless-phy* /ns-allinone-2.35/ns-2.35/mac/
cp maodv/ns-mcast.tcl /ns-allinone-2.35/ns-2.35/tcl/mcast/
cp Makefile /ns-allinone-2.35/ns-2.35/Makefile
cd /ns-allinone-2.35/ns-2.35/
make clean
make
make install
