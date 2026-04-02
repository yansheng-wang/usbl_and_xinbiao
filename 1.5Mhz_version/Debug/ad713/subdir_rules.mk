################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL = cmd.exe

# Each subdirectory must supply rules for building sources it contributes
ad713/%.obj: ../ad713/%.c $(GEN_OPTS) | $(GEN_FILES)
	@echo 'Building file: "$<"'
	@echo 'Invoking: C6000 Compiler'
	"D:/Program Files/ti/ccs831/ti-cgt-c6000_8.5.0.LTS/bin/cl6x" -mv6740 --include_path="D:/Projects/ccs831/1.5Mhz_version" --include_path="D:/Projects/ccs831/1.5Mhz_version/ad713" --include_path="D:/Projects/ccs831/1.5Mhz_version/no_os" --include_path="D:/BaiduNetdiskDownload/TL6748-EVM_V5.1/Demo/StarterWare/Include/DSPLib" --include_path="D:/BaiduNetdiskDownload/TL6748-EVM_V5.1/Demo/StarterWare/Include" --include_path="D:/BaiduNetdiskDownload/TL6748-EVM_V5.1/Demo/StarterWare/Include/StarterWare/Drivers" --include_path="D:/BaiduNetdiskDownload/TL6748-EVM_V5.1/Demo/StarterWare/Include/StarterWare/Drivers/c674x" --include_path="D:/BaiduNetdiskDownload/TL6748-EVM_V5.1/Demo/StarterWare/Include/StarterWare/Drivers/c674x/c6748" --include_path="D:/BaiduNetdiskDownload/TL6748-EVM_V5.1/Demo/StarterWare/Include/StarterWare/Drivers/hw" --include_path="D:/Program Files/ti/ccs831/ti-cgt-c6000_8.5.0.LTS/include" --define=c6748 -g --c99 --diag_warning=225 --diag_wrap=off --display_error_number --preproc_with_compile --preproc_dependency="ad713/$(basename $(<F)).d_raw" --obj_directory="ad713" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


