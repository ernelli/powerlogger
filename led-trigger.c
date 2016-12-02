#include <stdio.h>
#include <sys/time.h>
#include <string.h>
#include <string.h>
#include <errno.h>

#ifndef TEST_HTTP
#include <wiringPi.h>
#endif

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <netdb.h>


typedef long long timestamp;

timestamp getCurrentTime() {
  struct timeval time;
  gettimeofday(&time, NULL);
  
  return (timestamp)time.tv_sec*1000 + (timestamp)time.tv_usec/1000;
}


typedef long long timestamp_us;

timestamp_us getCurrentTime_us() {
  struct timeval time;
  gettimeofday(&time, NULL);
  
  return (timestamp_us)time.tv_sec*1000000 + (timestamp_us)time.tv_usec;
}


int http_client(const char *url, const char *method, unsigned char *entity_body, int len, const unsigned char *post_body, int post_len, const char *headers) {
  char scheme[32];
  char hostname[64];
  char path[256];
  int port = 80;
  int status_code = -1;
  FILE *fp = NULL;
  int s = -1;

  struct timespec start, end;

  if(sscanf(url, "%[^:]://", scheme) != 1) {
    return -1;
  }

  //printf("scheme: %s\n", scheme);

  // skip scheme://
  url += strlen(scheme) + 3;
  
  // port given
  if(strchr(url, ':')) {
    if(sscanf(url, "%[^:]:%d%s", hostname, &port, path) != 3) {
      return -1;
    }
    //printf("hostname: %s\nport: %d\npath: %s\n", hostname, port, path);
  } else {
    if(sscanf(url, "%[^/]%s", hostname, path) != 2) {
      return -1;
    }
    //printf("hostname: %s\nport: %d\npath: %s\n", hostname, port, path);
  }

  struct hostent *hostent = gethostbyname(hostname);

  if(!hostent) {
    goto cleanup;
  }

  //printf("addr_type: %d, AF_INET: %d\n", hostent->h_addrtype, AF_INET);
  //hexdump(hostent->h_addr_list[0], 16);
  //printf("lookup: %08X\n", *(unsigned int *)hostent->h_addr_list[0]);

  struct sockaddr_in addr;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  memcpy(&addr.sin_addr, hostent->h_addr_list[0], sizeof(addr.sin_addr));

  s = socket(AF_INET, SOCK_STREAM, 0);

  if(s == -1) {
    goto cleanup;
  }

  //printf("connect to: %08X:%d\n", *(unsigned int *)&addr.sin_addr, port);
  
  if(connect(s, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
    printf("connect failed: %s\n", strerror(errno));
    goto cleanup;
  }

  fp = fdopen(s, "r+");

  char contentLengthHeader[40];
  contentLengthHeader[0] = '\0';

  if(post_len) {
    sprintf(contentLengthHeader, "content-length: %d\r\n", post_len);
  }

  fprintf(fp, "%s %s HTTP/1.1\r\n%shost: %s\r\n%sconnection: close\r\n\r\n", method, path, headers ? headers: "", hostname, contentLengthHeader);

  if(post_len) {
    if(fwrite(post_body, 1, post_len, fp) != post_len) {
      goto cleanup;
    }
  }

  fflush(fp);
  
  char status[4096];
  char buffer[4096];

  if(!fgets(status, sizeof(status), fp)) {
    goto cleanup;
  }

  //printf("%s", status);

  int chunked = 0;

  while(fgets(buffer, sizeof(buffer), fp)) {
    //printf("header: %s", buffer);

    if(strcmp(buffer, "\r\n") == 0) {
      break;
    }

    if(strcasecmp(buffer, "transfer-encoding: chunked\r\n") == 0) {
      chunked = 1;
    }
  }

  // read entity body

  char *d = entity_body;
  int chunk_size;

  if(chunked) {

    do {
      //fgets(buffer, sizeof(buffer), fp)

      if(fscanf(fp, "%x\r\n", &chunk_size) != 1) {
        break;
      }
      
      //printf("read chunk_size: %d\n", chunk_size);

      if(chunk_size >= len) {
        chunk_size = len - 1;
      }


      if(fread(entity_body, 1, chunk_size, fp) != chunk_size) {
        goto cleanup;
      }

      entity_body += chunk_size;

      len -= chunk_size;

      if(fscanf(fp, "\r\n") == EOF) {
        goto cleanup;
      }

    } while(len && chunk_size);
    
  } else {
    entity_body += fread(entity_body, 1, len, fp);
  }

  *entity_body = '\0';

  if(sscanf(status, "HTTP/%*d.%*d %d", &status_code) != 1) {
    status_code = -1;
  }

 cleanup:
  if(fp) {
    fclose(fp);
  } else if(s != -1) {
    close(s);
  }

  return status_code;
}



unsigned int lastTrigger = 0;

int power = 0;
int kwh = 0;

void edge_trigger_cb() {
  //unsigned int delta = 0, now;
  timestamp_us delta = 0, now;

  now = getCurrentTime_us(); //millis();

  if(lastTrigger) {
    delta = now - lastTrigger;
  }

  lastTrigger = now;

  kwh++;
  if(delta) {
    power = (int)(3600LL*1000000LL/delta);
  }

  printf("timestamp: %Ld, power: %d\n", now/1000, power);
}




int main (int argc, char *argv[])
{
  if(argc >= 2) {
    char sendbuffer[4096];
    char response[4096];
    memset(sendbuffer, 0, sizeof(sendbuffer));
    fread(sendbuffer, 1, sizeof(sendbuffer), stdin);
    printf("sending: %s\n", sendbuffer);
    int statusCode = http_client(argv[1], "POST", response, sizeof(response), sendbuffer, strlen(sendbuffer), "content-type: application/json\r\n");
    if(statusCode == -1) {
      printf("Failed: %s\n", strerror(errno));
    } else {
      printf("got server response, code: %d\n%s", statusCode, response);
    }
  }

#ifndef TEST_HTTP
  printf("calling wiringPi setup\n");
  wiringPiSetup () ;


  printf("setup edge ISR\n");

  if(wiringPiISR (0, INT_EDGE_FALLING,  edge_trigger_cb) != 0) {
    printf("Failed to setup ISR");
    return 1;
  } 


  printf("edge trigger setup, waiting for events\n");

  while(1) {
    sleep(10);
    printf("10 seconds passed, kWh: %d.%d, power: %d\n", kwh/1000, kwh%1000, power);
  }

  return 0 ;
#endif
}
