packets = []


class Packet:

    count = 0

    def __init__(self, src, dst, type):
        self.src = src
        self.dst = dst
        self.path = []
        self.path.append(src)
        self.id = Packet.count
        Packet.count += 1
        self.type = type


class Router:
    def __init__(self):
        self.cache = {}
        self.neighbours = []

    def send_rreq(self, dst):
        packet = Packet(self.id, dst, "rreq")
        self.broadcast(packet)

    def broadcast(self, packet):
        for i in self.neighbours:
            if i not in packet.path:
                packet.next = i
                packets.append(packet)

    def recv_rreq(self, packet):
        if packet.dst in self.cache:
            self.send_rrep(packet)
        else:
            packet.path.append(self.id)
            self.broadcast(packet)

    def send_rrep(self, packet):
        pkt = Packet(self.id, packet.src, "rrep")
        pkt.path = packet.path.reverse()
        pkt.path = pkt.path[-1:]
        pkt.next = pkt.path[0]
        packets.append(pkt)

    def recv_rrep(self, packet):
        self.updateCache(packet.data)
        if packet.dst != self.id:
            packet.path = packet.path[-1:]
            packet.next = packet.path[0]
            packets.append(packet)
