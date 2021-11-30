BEGIN {
    pkt=0;
    time=0;
    # printf("title = Throughput\n");
}
{
    if($1=="r") {
        pkt = pkt + $6;
        time=$2;
        printf("%f\t%d\n", time, pkt);
    }
}
END {
    
}