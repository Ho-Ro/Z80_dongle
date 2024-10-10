tinybasic2.hex: tinybasic2.obj
	srec_cat $< -binary -output $@ -intel

tinybasic2.obj: tinybasic2.asm
	uz80as --target=i8080 $<
