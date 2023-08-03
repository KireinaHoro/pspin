all::
	make deploy
	make driver
	make driver_debug

../generic_driver/gdriver_args.c: ../generic_driver/gdriver_args.ggo
	gengetopt -i ../generic_driver/gdriver_args.ggo -F gdriver_args --output-dir=../generic_driver/

SPIN_DRIVER_CC ?= gcc

driver: driver/driver.c ../generic_driver/gdriver_args.c ../generic_driver/gdriver.c
	 $(SPIN_DRIVER_CC) -std=c99 -I../generic_driver/ -I$(PSPIN_RT)/runtime/include/ -I$(PSPIN_HW)/verilator_model/include $(SPIN_DRIVER_CFLAGS) driver/driver.c ../generic_driver/gdriver.c ../generic_driver/gdriver_args.c -L$(PSPIN_HW)/verilator_model/lib/ -lpspin $(SPIN_DRIVER_LDFLAGS) -o sim_${SPIN_APP_NAME}

driver_debug: driver/driver.c ../generic_driver/gdriver_args.c
	 $(SPIN_DRIVER_CC) -g -std=c99 -I../generic_driver/ -I$(PSPIN_RT)/runtime/include/ -I$(PSPIN_HW)/verilator_model/include $(SPIN_DRIVER_CFLAGS) driver/driver.c ../generic_driver/gdriver.c ../generic_driver/gdriver_args.c -L$(PSPIN_HW)/verilator_model/lib/ -lpspin_debug $(SPIN_DRIVER_LDFLAGS) -o sim_${SPIN_APP_NAME}_debug

clean::
	-@rm *.log 2>/dev/null || true
	-@rm -r build/ 2>/dev/null || true
	-@rm -r waves.vcd 2>/dev/null || true
	-@rm sim_${SPIN_APP_NAME} 2>/dev/null || true
	-@rm sim_${SPIN_APP_NAME}_debug 2>/dev/null || true

run::
	./sim_${SPIN_APP_NAME} | tee transcript

.PHONY: driver driver_debug clean run
