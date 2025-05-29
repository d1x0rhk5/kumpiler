#include <stdio.h>

int main(void) {
    int b = 4;
    int a_len = 10;
    int a[10] = {1,2,3,4,5,6,7,8,9,10};
    int taekwan_len = 12345;
    int taekwan[12345];
    long junsung;
    double temp;
    int chan_len = 12;
    char chan[12];
    for(int i=0; i<10; i++) {
        printf("%d ", a[i]);
    }
    printf("\na[-1]: %d", a[-1]);
    return 0;
}
