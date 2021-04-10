/*
 * Приложение поддержки лифтовой этажной кнопки вызова ВП-53
 * 
 * Подключение битов FT232:
 * 	D0 (вход)	- кнопка основная			(лог. 0 - нажата)
 * 	D2 (выход)	- индикатор жёлтый			(лог. 1 - вкл.)
 * 	D1 (выход)	- индикатор зелёный			(лог. 1 - вкл.)
 *	D3 (выход)	- индикатор красный			(лог. 1 - вкл.)
 *	D6 (выход)	- магнит основной кнопки	(лог. 1 - вкл.)
 * 	D5, D7, D4	- не используются
 */
 
 // https://www.intra2net.com/en/developer/libftdi
 
 // usb-ftdi.rules
 /*
#FT232RL Adapter
ACTION=="add", SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTRS{idProduct}=="6001", GROUP:="libftdi", MODE:="0660"
*/
 
 // sudo groupadd -f libftdi
 // sudo usermod -a -G uucp alexzhak
 
 
// ./vp53app "/home/alexzhak/Рабочий стол/Проекты_Дом/ВП-53/App" "make clean; make all" "2>" "tmp.txt" "warning" "error" "geany"

#define EXIT_FAILURE	-1
#define EXIT_SUCCESS	0

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libftdi1/ftdi.h>

// VID/PID микросхемы FT232RL
#define FTDI_VID	0x0403
#define FTDI_PID	0x6001

// Описание устройства в EEPROM
#define VP53_DESC	"VP-53 elevator button"

#define BITMASK_OUT			0b01001110	// Маска пинов (1 - выход, 0 - вход)
// Кнопка
#define BB_BUTTON_MAIN		0b00000001
// Индикаторы
#define BB_LIGHT_RED		0b00001000	// красный
#define BB_LIGHT_YELLOW		0b00000100	// жёлтый
#define BB_LIGHT_GREEN		0b00000010	// зелёный
// Электромагнит залипайки
#define BB_MAGNET			0b01000000
/* --- Зависят от ОС --- */
// Имя временного файла для лога
//#define TEMP_FILE_NAME		"/tmp/vp53tmp.txt"
//#define CLI_OUTPUT_REDIRECT "%s > %s"

/* Обёртка для вывода в консоль */
void logPrintf(const char *format, ...)
{
	va_list vl;
	va_start(vl, format);
	vprintf(format, vl);
	va_end(vl);
	fflush(stdout);
}

/* Поиск фрагмента в тексте */
char findText(char buffer[], unsigned long buffer_length, char word_to_find[])
{
	unsigned long word_pos=0;
	for(unsigned long buffer_pos=0; buffer_pos<buffer_length; buffer_pos++)
	{
		if(buffer[buffer_pos] == word_to_find[word_pos])
		{
			word_pos++;
			if(word_pos == strlen(word_to_find)) return 0xFF;
		}
		else
			word_pos=0;
	}
	return 0x00;
}

/* Выполнение скрипта и парсинг лога ответа */
char scriptDo(char script_name[], char redirection[], char warning_word[], char error_word[], char out_save_name[])
{
	// Выполняем команду
	char do_command[2048];
	sprintf(do_command, "%s %s %s", script_name, redirection, out_save_name);
	logPrintf("\r\nКоманда={%s}\r\n",do_command);
	system(do_command);
	logPrintf("\r\n");
	// открываем выходной log-файл
	FILE *outlog = fopen(out_save_name,"rb");
	if(!outlog)
	{
		logPrintf("\r\n[ОШИБКА, нет доступа к log-файлу с результатом.]");
		return 0;
	}
	fseek(outlog, 0 , SEEK_END);
	unsigned long logsize=ftell(outlog);
	char *textbuffer = (char*)malloc(logsize);
	fseek(outlog, 0 , SEEK_SET);
	if(!textbuffer)
	{
		logPrintf("\r\n[ОШИБКА, нельзя выделить память.]");
		fclose(outlog);
		return 0;
	}
	fread((void*)textbuffer, logsize, 1, outlog);
	fclose(outlog);
	// Парсим log-файл
	char color_to_return=0;
	
	if(findText(textbuffer, logsize, warning_word)) color_to_return|=BB_LIGHT_YELLOW;
	if(findText(textbuffer, logsize, error_word)) color_to_return|=BB_LIGHT_RED;
	if(!color_to_return) color_to_return = BB_LIGHT_GREEN;

	free(textbuffer);
	return color_to_return;
}

int main(int argc, char *argv[], char *env[])
{
	// Переменные
	char *working_dir = (char*) NULL;
	char *script_name;
	char *redirection;
	char *temp_log_name;
	char *warning_word;
	char *error_word;
	char *notepad_name;
	char isSetup=0x00;
	
	// Приветствие
	logPrintf("\r\n=== ВП-53 USB ===");
	
	// Обработка параметров командной строки
	if(argc==2 && !strcmp(argv[1], "setup"))
		isSetup=0xFF;
	else if(argc!=8)
	{
		logPrintf("\r\n[ОШИБКА, неверное кол-во аргументов командной строки. Должно быть 7: working_dir, script_name, redirection, temp_log_name, warning_word, error_word, text_editor_name или 1 аргумент setup для первичной настройки микросхемы]\r\n");
		return EXIT_FAILURE;
	}
	else
	{
		working_dir=argv[1];
		script_name=argv[2];
		redirection=argv[3];
		temp_log_name=argv[4];
		warning_word=argv[5];
		error_word=argv[6];
		notepad_name=argv[7];
	}
	
	// Меняем рабочий каталог
	logPrintf("\r\nМеняем рабочий каталог...");
	if(working_dir)
	{
		int rcd=chdir(working_dir);
		if(rcd)
		{
			logPrintf("\r\n[ОШИБКА, не удалось установить рабочий каталог]\r\n");
			return EXIT_FAILURE;
		}
		logPrintf("[УСПЕХ]");
	}
	
	// Инициализация библиотеки libftdi
	struct ftdi_context ftdic;
	logPrintf("\r\nИнициализация libftdi... ");
	int ret=ftdi_init(&ftdic);
	if(ret!=0)
	{
		logPrintf("[ОШИБКА номер: %i]\r\n", ret);
		return EXIT_FAILURE;
	}
	logPrintf("[УСПЕХ]");
	
	// Ожидаем подключения устройства и используем его, либо настраиваем, если задан ключ setup
	struct libusb_device *dev_found;
	search_for_device:
	do
	{
		dev_found = (struct libusb_device *)NULL;
		logPrintf("\r\nВыдержка 3 секунды...");
		sleep(3);
		
		// Листинг всех устроств с FTDI_VID и FTDI_PID
		logPrintf("\r\nПоиск устройств FTDI (VID=%#06x; PID=%#06x)... ", FTDI_VID, FTDI_PID);
		struct ftdi_device_list *devlist;
		int usb_search = ftdi_usb_find_all(&ftdic, &devlist, FTDI_VID, FTDI_PID);
		if(usb_search<0)
			logPrintf("[ОШИБКА номер: %i]", ret);
		else if(usb_search==0)
			logPrintf("[ОШИБКА, устройств не найдено]");
		else
		{
			logPrintf("[УСПЕХ, устройств найдено: %i]", usb_search);
			struct ftdi_device_list *link_next=devlist;
			for(int i=0; i<usb_search; i++)
			{
				struct libusb_device *dev = link_next->dev;
						
				logPrintf("\r\n	№%i: ", i+1);
				
				char description[256],serial[256];
				ret=ftdi_usb_get_strings2(&ftdic, dev, (char*)NULL, 0, description, 256, serial, 256);
				if(ret!=0)
					logPrintf("[ОШИБКА номер: %i]", ret);
				else
				{
					logPrintf(" Описание=\"%s\" С/н=\"%s\"", description, serial);
					if(!dev_found && !isSetup)
					{
						if(!strcmp(description, VP53_DESC))
						{
							dev_found = dev;
							logPrintf(" <выбрано>");
						}
					}
				}
				
				link_next = link_next->next;
			}
		}
		
		// Если задан ключ setup (настройка нового устройства)
		if(isSetup)
		{
			logPrintf("\r\n{Настройка нового устройства}");
			if(usb_search==1)
			{
				logPrintf("\r\n	Вы уверены? ( 1-Да/ 0-Нет ↩ )");
				fflush(stdin);
				int answer;
				scanf("%i", &answer);
				if(answer==1)
				{
					// Открываем устройство
					logPrintf("\r\n	Пытаемся открыть устройство... ");
					ret=ftdi_usb_open_dev(&ftdic, devlist->dev);
					if(ret!=0)
					{
						logPrintf("[ОШИБКА номер: %i]", ret);
						goto eeprom_write_fail;
					}
					logPrintf("[УСПЕХ]");
					
					// Чтение образа EEPROM
					logPrintf("\r\n	Чтение образа EEPROM... ");
					ret = ftdi_read_eeprom(&ftdic);
					if(ret!=0)
					{
						logPrintf("[ОШИБКА номер: %i]", ret);
						goto eeprom_write_fail;
					}
					logPrintf("[УСПЕХ]");

					// Декодирование EEPROM
					logPrintf("\r\n	Декодирование EEPROM... ");
					ret=ftdi_eeprom_decode(&ftdic,0);
					if(ret!=0)
					{
						logPrintf("[ОШИБКА номер: %i]", ret);
						goto eeprom_write_fail;
					}
					logPrintf("[УСПЕХ]");

					// Получаем данные из структуры EEPROM
					logPrintf("\r\n	Извлечение данных из структуры EEPROM... ");
					char manufacturer[256],description[256],serial[256];
					int max_power;
					ret = ftdi_eeprom_get_strings(&ftdic, manufacturer, 256, description, 256, serial, 256);
					if(ret==0) ftdi_get_eeprom_value(&ftdic, MAX_POWER, &max_power);
					if(ret!=0)
					{
						logPrintf("[ОШИБКА]");
						goto eeprom_write_fail;
					}
					logPrintf("[УСПЕХ]");
					
					// Выводим полученные данные
					logPrintf("\r\n	Текущие данные:");
					logPrintf("\r\n		Производитель = \"%s\"\r\n		Описание = \"%s\"\r\n		С/н = \"%s\"\r\n		Макс. ток = %iмА", manufacturer, description, serial, max_power);
					
					// Пишем в структуру EEPROM новые данные
					strcpy(description,VP53_DESC);
					max_power=500;
					logPrintf("\r\n	Запись обновлённых данных в структуру EEPROM... ");
					ret = ftdi_eeprom_set_strings(&ftdic, manufacturer, description, serial);
					if(ret==0) ftdi_set_eeprom_value(&ftdic, MAX_POWER, max_power);
					if(ret!=0)
					{
						logPrintf("[ОШИБКА]");
						goto eeprom_write_fail;
					}
					logPrintf("[УСПЕХ]");
					
					// Кодирование EEPROM	
					logPrintf("\r\n	Кодирование EEPROM... ");
					ret = ftdi_eeprom_build(&ftdic);
					if(ret<0)
					{
						logPrintf("[ОШИБКА номер: %i]", ret);
						goto eeprom_write_fail;
					}
					logPrintf("[УСПЕХ, объём %i байт]", ret);
					
					// Запись образа EEPROM
					logPrintf("\r\n	Запись образа EEPROM... ");
					ret = ftdi_write_eeprom(&ftdic);
					if(ret!=0)
					{
						logPrintf("[ОШИБКА номер: %i]", ret);
						goto eeprom_write_fail;
					}
					logPrintf("[УСПЕХ]");
					
					// Закрываем устройство и выходим
					ftdi_usb_close(&ftdic);
					ftdi_list_free(&devlist); 
					logPrintf("\r\n{Настройка завершена, перезапустите приложение}\r\n");
					return EXIT_SUCCESS;
					
					eeprom_write_fail:
					ftdi_usb_close(&ftdic);
					ftdi_list_free(&devlist); 
					logPrintf("\r\n{Ошибка при настройке. Попробуйте ещё раз.}\r\n");
					return EXIT_FAILURE;
				}
				else
					logPrintf("\r\n{Настройка отменена}");
			}
			else if(!usb_search)
				logPrintf("\r\nВнимание! Подключите настраиваемое устройство!");
			else
				logPrintf("\r\nВнимание! Оставьте подключённым только настраиваемое устройство!");
		}
		else
		{
			logPrintf("\r\nКнопка ВП-53 ");
			
			if(dev_found)
			{
				logPrintf("найдена");
				ftdi_list_free(&devlist);
				break;
			}
			else
				logPrintf("не найдена");
		}
		
		ftdi_list_free(&devlist); 	
		
	}
	while(1);
	
	// Открываем устройство
	logPrintf("\r\nПытаемся открыть устройство... ");
	ret=ftdi_usb_open_dev(&ftdic, dev_found);
	if(ret!=0)
	{
		logPrintf("[ОШИБКА номер: %i]", ret);
		goto main_fail;
	}
	logPrintf("[УСПЕХ]");
	
	// Включаем BitBang
	logPrintf("\r\nВключаем BitBang... ");
	ret=ftdi_set_bitmode(&ftdic, BITMASK_OUT, BITMODE_BITBANG);
	if(ret!=0)
	{
		logPrintf("[ОШИБКА номер: %i]", ret);
		goto main_fail;
	}
	logPrintf("[УСПЕХ]");
	logPrintf("\r\n[ГОТОВ]");
	
	#define STATE_UNPRESSED			0
	#define STATE_ENABLE_MAGNET		1
	#define STATE_RUN_SCRIPT		2
	#define STATE_RESULT			3
	char button_state=STATE_UNPRESSED;
	char lights_and_magnet=BB_LIGHT_RED|BB_LIGHT_GREEN|BB_LIGHT_YELLOW;
	ret=ftdi_write_data(&ftdic, &lights_and_magnet, 1);
	if(ret!=1) goto main_fail;
	do
	{
		usleep(50 * 1000);
		switch(button_state)
		{
			case STATE_UNPRESSED:
			{
				// Читаем состояние кнопки
				char buttons;
				ret=ftdi_read_pins(&ftdic, &buttons);
				if(ret!=0) goto main_fail;
				if(!(buttons&BB_BUTTON_MAIN))
				{
					button_state=STATE_ENABLE_MAGNET;
					logPrintf("\r\n[Нажата кнопка]");
				}
			}
			break;
			
			case STATE_ENABLE_MAGNET:
			{
				lights_and_magnet=BB_MAGNET;
				// Устанавливаем состояние индикаторов и магнита
				ret=ftdi_write_data(&ftdic, &lights_and_magnet, 1);
				if(ret!=1) goto main_fail;
				logPrintf("\r\n[Кнопка залипла]");
				button_state=STATE_RUN_SCRIPT;
				usleep(1000 * 1000);
			}
			break;
			
			case STATE_RUN_SCRIPT:
			{
				logPrintf("\r\n[Выполняем скрипт]");
				lights_and_magnet&=~(BB_MAGNET|BB_LIGHT_RED|BB_LIGHT_GREEN|BB_LIGHT_YELLOW);
				//char script_string[2048];
				//sprintf(script_string, "%s >> %s", script_name, TEMP_FILE_NAME);
				lights_and_magnet|=scriptDo(script_name, redirection, warning_word, error_word, temp_log_name);
				button_state=STATE_RESULT;
			}
			break;
			
			case STATE_RESULT:
			{
				logPrintf("\r\n[Выводим результат]");
				ret=ftdi_write_data(&ftdic, &lights_and_magnet, 1);
				if(ret!=1) goto main_fail;
				// Выводим лог только если были ошибки/предупреждения
				if((lights_and_magnet&BB_LIGHT_RED) || (lights_and_magnet&BB_LIGHT_YELLOW))
				{
					char notepad_run[2048];
					sprintf(notepad_run, "%s %s", notepad_name, temp_log_name);
					logPrintf("\r\n");
					system(notepad_run);
					logPrintf("\r\n");
					remove(temp_log_name);
				}
				// Задержка 1 сек
				usleep(1000 * 500);
				button_state=STATE_UNPRESSED;
			}
			break;
			
		}
	}
	while(1);
	
	main_fail:
	logPrintf("\r\n{Ой, что-то пошло не так...}");
	ftdi_usb_close(&ftdic);
	goto search_for_device;
}
