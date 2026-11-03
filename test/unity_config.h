/* Wyjście Unity: na STM32 przez VCP ST-LINK (USART1), na hoście domyślne stdout. */
#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

#ifdef STM32F746xx
void unity_output_start(void);
void unity_output_char(int c);
void unity_output_flush(void);
void unity_output_complete(void);

#define UNITY_OUTPUT_START()    unity_output_start()
#define UNITY_OUTPUT_CHAR(c)    unity_output_char(c)
#define UNITY_OUTPUT_FLUSH()    unity_output_flush()
#define UNITY_OUTPUT_COMPLETE() unity_output_complete()
#endif

#endif /* UNITY_CONFIG_H */
