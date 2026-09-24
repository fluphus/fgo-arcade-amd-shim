#ifndef PRESENT_DXGI_LATENCY1_H
#define PRESENT_DXGI_LATENCY1_H

/* Keep the AMD GL flip chain and request interval 1, with chain maximum
   frame latency 1 when supported. This does not enable WGL swap interval 1
   or add a CPU wait to Present. */
int present_dxgi_latency1_enabled(void);
void present_dxgi_latency1_install(int allow_load);

#endif
