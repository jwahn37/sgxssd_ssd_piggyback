#include "sha256_sgx.h"
#include "ftl.h"
//#include<stdio.h>
//#include<string.h>
const int input_blocksize = 64; //SHA-1 SHA-256 SHA-512 RFC2104,RFC4868
#define	TIMER_CH1		1
#define	TIMER_PRESCALE_0			(0 << 2)	// 1
//키 길이가 B보다 크면 해시
//키 길이 = 해시 함수의 출력 길이.
//key:128bit
/*
int main()
{
	hmac_test();
//	test();
	return 0;
}
*/
/*
void hmac_test()
{
    unsigned char h_mac[SHA256_BLOCK_SIZE] = {0,}; //해쉬된 값을 저장하기 ㅜ이한 버퍼
    unsigned char h_mac2[SHA256_BLOCK_SIZE] = {0,}; //해쉬된 값을 저장하기 ㅜ이한 버퍼
    unsigned char key[KEY_SIZE];
    int i;
    unsigned char data[8192];

	for(i=0; i<8192; i++)
		data[i]=i%128;
    for(i=0; i<KEY_SIZE; i++)
    {
        key[i]=i/128;
    }
    uart_printf("hash test start:");
    //ptimer_start(); 
//            ptimer_stop_and_uart_print(); 
    HMAC(key,h_mac, data,8192);
    //    ptimer_start(); 

    //ptimer_stop_and_uart_print(); 
    HMAC(key,h_mac2, data,8192);
   
    uart_printf("hash test result:");
    uart_printf("%d",memcmp(h_mac, h_mac2, HASHED_OUTPUT));

	for(i=0; i<HASHED_OUTPUT; i++)
		uart_printf("%x",h_mac[i]);

	
   //a>b) ? a: b
//    (memcmp(h_mac, h_mac2, HASHED_OUTPUT)==0)? (uart_printf("hash success !");) : (uart_printf("hash failed!\n"));
}
*/
void Hash(const unsigned char data[],const int data_len, unsigned char h_mac[], SHA256_CTX* ctx)
{
  //  uart_print("init");
    sha256_init(ctx);
 //   uart_print("upadate"); 
    sha256_update(ctx, data, data_len); //여기서 오버헤드가 몰린다.
 //   uart_print("fin");
    sha256_final(ctx, h_mac);
}
/*
void HMAC_DELAY(int time)
{
   
    start_interval_measurement(TIMER_CH1, TIMER_PRESCALE_0); //timer start
    UINT32 rtime;
    char buf[21];

    do{
        rtime = 0xFFFFFFFF - GET_TIMER_VALUE(TIMER_CH1);
	//uart_printf("%d );
    // Tick to us
        rtime = (UINT32)((UINT64)rtime * 2 * 1000000 * 
	    PRESCALE_TO_DIV(TIMER_PRESCALE_0) / CLOCK_SPEED);
    }
    while(rtime<time);
  //  sprintf(buf, "%u", rtime);
  //  uart_print(buf);
	///uart_printf("clock sped : %d\n", CLOCK_SPEED);

    //hash1 은 delay줄때 들어가는 datasize = in
    //input_blocksize+data_size = 64+data_size
    //input_blocksize +HASHED_OUTPUT = 64+32
 //..Hash(data,input_blocksize + text_size,  h_mac, &ctx);  // O(hash(data size + 256biy))
 //..// Hash(data, input_blocksize + HASHED_OUTPUT, h_mac, &ctx); //O(hash(256+64bit))   
}
*/
/*
void HMAC(const unsigned char *key, unsigned char *h_mac, const unsigned char *text, const int text_size)
{
    //delay 주고 리턴
    int i;
    for(i=0; i<32; i++)
        h_mac[i]= i;
    
}
*/

void HMAC(const unsigned char key[], unsigned char h_mac[], const unsigned char text[], const int text_size)
{
	unsigned char Ki[HASH_BLOCK_SIZE] = {0,}; // K0 ^ ipad
	unsigned char Ko[HASH_BLOCK_SIZE] = {0,}; //K0 ^ opad
    const int DATA_BUFFERLEN = text_size + input_blocksize + HASHED_OUTPUT + 1;   //8192+64+32+1 = 
    //char *data;
    UINT32 data = HMAC_BUFF+DRAM_ECC_UNIT-input_blocksize;
    //char data[DATA_BUFFERLEN];
    uart_printf("HMAC??");
	//char data[DATA_BUFFERLEN] = {0,}; //중간계산값을 저장하기 위한 버퍼
	//char hashed_data[HASHED_OUTPUT+1] = {0,}; //해쉬된 값을 저장하기 ㅜ이한 버퍼
    int i;
//	int key_len = (128/8);
    //data = (char*) malloc(sizeof(char)*DATA_BUFFERLEN);
    //for(i=0; i<4096; i++)
      //  uart_printf("%x", data[i]);
    //uart_printf("key : %x %x %x %x ", key[0],key[1],key[2],key[3]);

    //_mem_set_sram(data, 0x00, DATA_BUFFERLEN);
   // mem_set_dram(data, 0x00, DATA_BUFFERLEN);
   // memset(data, 0x00, DATA_BUFFERLEN);

    SHA256_CTX ctx;
    //여긴 돌아갈일없음.
    
    /*
    if(KEY_SIZE > input_blocksize)
    {
        Hash(key,KEY_SIZE, key, ctx); 
        KEY_SIZE = HASHED_OUTPUT; //OUTPUT=256bit일것임
    }
    */
   
    //else
    //_mem_copy(Ki, key, KEY_SIZE);
    //_mem_copy(Ko, Ki, KEY_SIZE);
    //uart_print("keyalloc");
    for(i=0; i<KEY_SIZE; i++)
    {
        Ki[i] = Ko[i] = key[i];
    }
    //memcpy(Ki, key, KEY_SIZE);
    //memcpy(Ko, Ki, KEY_SIZE);        //Ko, Ki는 해쉬된 키값

//이후 B만큼 나머지 길이를 0으로 채운다 여기도 돌아갈일없음.
    for(i=KEY_SIZE; i<input_blocksize; i++)
    {
        Ki[i]=0x00;
        Ko[i]=0x00;
    }
    //ipad opad를 이용해서 Ko를 미리 계산한다.
    for(i=0; i<input_blocksize; i++)
    {
        Ki[i] ^= 0x36;
        Ko[i] ^= 0x5c;
    }
    //uart_print("mcpy");
    //위에서 계산한 ;Ki ^ ipad와 HMAC대상인 test를 연접
    _mem_copy(data, Ki, input_blocksize);
   // _mem_copy(data+input_blocksize, text, text_size);  //여기서 data길이 = 자른KI(64bit) + data임
    //해시한다.
    //test는 여기 생략
   // uart_print("hash");
    Hash(data,input_blocksize + text_size,  h_mac, &ctx);  // O(hash(data size + 256biy))
   // uart_print("hashf");
    //Ko ^ opad와 위에 해쉬 결과를 연접
   // _mem_set_sram(data, 0x00, DATA_BUFFERLEN);
    mem_set_dram(data, 0x00, HMAC_SIZE);
    _mem_copy(data, Ko, input_blocksize);
    _mem_copy(data+input_blocksize, h_mac, HASHED_OUTPUT);   //여기서 data길이 = 64bit+256bit(hashed)
    Hash(data, input_blocksize + HASHED_OUTPUT, h_mac, &ctx); //O(hash(256+64bit))

    //예측 복잡도 = O( hash(128) + hash(data+256bit) + hash(320)) //결국 O(Hash(data)) 랑 비슷.
    //=O(hash(data))
}


