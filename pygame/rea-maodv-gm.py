import math

packets = []

NO_OF_RETRIES = 3

energy_transmission = 0.3

energy_receive = 0.1


class Packet:

    count = 0

    def __init__(self, src, dst, type):
        self.src = src
        self.dst = dst
        self.path = []
        self.path.append(src)
        self.no_of_hops = 1
        self.id = Packet.count
        Packet.count += 1
        self.type = type
        self.t_energy = 0


class Router:
    def __init__(self):
        self.cache = {}
        self.neighbours = []
        self.group_number = None
        self.energy = 5

    def send_rreq(self, dst):
        packet = Packet(self.id, dst, "rreq")
        self.broadcast(packet)

    def broadcast(self, packet):
        for i in self.neighbours:
            if i not in packet.path:
                packet.next = i
                self.energy -= energy_transmission
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
        packet.t_energy += self.energy
        packets.append(pkt)

    def recv_rrep(self, packet):
        self.updateCache(packet.data)
        if packet.dst != self.id:
            packet.path = packet.path[-1:]
            packet.next = packet.path[0]
            self.energy -= energy_transmission
            packets.append(packet)

    def send_rreq_j(self, groupNumber):
        self.group_number = groupNumber
        packet = Packet(self.id, None, "rreq_j")
        self.broadcast(packet)

    def recv_rreq_j(self, packet):
        self.energy -= energy_receive
        if packet.src.group_number == self.group_number:
            self.send_rrep_j(packet)
        else:
            packet.path.append(self.id)
            self.broadcast(packet)

    def send_rrep_j(self, packet):
        pkt = Packet(self.id, packet.src, "rrep_j")
        pkt.t_energy *= self.energy
        pkt.path = packet.path.reverse()
        pkt.path = pkt.path[-1:]
        pkt.next = pkt.path[0]
        packets.append(pkt)

    def recv_rrep_j(self, packet):
        self.energy -= energy_transmission
        self.group_number = packet.dst.group_number
        if packet.dst != self.id:
            packet.path = packet.path[-1:]
            packet.next = packet.path[0]
            packets.append(packet)
        else:
            calculate_energy(packet.t_energy, packet.hop_count)
            self.updateCache(self, packet, packet.energy)


def calculate_energy(e, c, i=5):
    return math.pow(e, 1/c) * i