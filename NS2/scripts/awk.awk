BEGIN {
    totalRecievers = 10;
    totalNodes = 50;
    totalPid = 20000;
    i = 0;
    printf("Initializing Analysis process");
    while (i<totalPid) {
        send[i] = 0;
        sendTime[i] = 0;
        j = 0;
        while(j<totalNodes) {
            recv[i][j] = 0;
            latency[i][j] = 0;
            j += 1;
        }
        i += 1;
    }
}

{
    event = $1;
    current_time = $3;
    node_id = $5;
    packet = $19;
    pkt_id = $41;
    flow_id = $39;
    packet_size = $37;
    flow_type = $45; //CBR
    if (flow_type == "cbr" && event == "s" && packet == "AGT") {
        send[pkt_id] = 1;
        sendTime[pkt_id] = current_time;
    } else if (flow_type == "cbr" && event == "r" && packet == "RTR") {
        if (recv[pkt_id][node_id] == 0) {
            recv[pkt_id][node_id] = 1;
            latency[pkt_id][node_id] = current_time - sendTime[pkt_id];
        }
    }
}

END {
    printf("Summarizing...");
    totalSend = 0;
    totalRecv = 0;
    totalLatency = 0;
    i = 0;
    while (i < totalPid) {
        if (send[i] == 1) {
            totalSend += 1;
            j = totalNodes - totalRecievers;
            if (j == 0) {
                j = 1;
            }
            while (j < totalNodes) {
                if (recv[i][j] == 1) {
                    totalRecv += 1;
                    totalLatency += latency[i][j];
                }
                j += 1;
            }
        }
        i += 1;
    }
    printf("The number of receivers is %d\n", totalRecievers);
    printf("The number of sent-out packets is %d\n", totalSend);
    printf("The number of received packets is %d\n", totalRecv);
    if (totalRecievers != totalNodes) {
        print("Ratio is %f\n", totalRecv/(totalSend*totalRecievers));
    } else {
        print("Ratio is %f\n", totalRecv/(totalSend*(totalRecievers - 1)));
    }
    printf("Latency is %f\n", totalLatency/totalRecv);
}
