BEGIN {
	# printf("title = PDR\n");
}

{
    for(i=0; i<5; i++) {
        if($2 >= 10.000*i && $2 < 10.000*(i+1)) {
            if(($1 == "+" || $1 == "d")) {
                sent[i]++;
            }
            if($1 == "r") {
                recv[i]++;
            }
        }
    }
}

END {
    for(i=0; i<5; i++) {
        printf("%d %f\n", (i+1)*10, recv[i]/sent[i]);
    }
}