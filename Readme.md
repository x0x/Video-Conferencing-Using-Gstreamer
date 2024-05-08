
# Project Title

Video Conferencing Application Using Gstreamer 



## Run Locally

Clone the C file

Go to that directory

Compile the code linking the gstreamer library



```bash
  gcc app.c  `pkg-config --cflags --libs gstreamer-1.0` && ./a.out <Destination_IP>  <Destination_PORT> <Source_PORT>
```

Example Usage

Stream to 10.10.28.204 IP at port 7001. Receive the Stream at port 7002

```
gcc app.c  `pkg-config --cflags --libs gstreamer-1.0` && ./a.out 10.10.28.204 7001 7002


