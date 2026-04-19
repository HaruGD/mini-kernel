#dd if=/dev/zero of=./bin/disk.img bs=512 count=2880
#mkfs.fat -F 12 ./bin/disk.img
#mkdir -p /tmp/fatmount
#sudo mount ./bin/disk.img /tmp/fatmount
#echo "Hello from FAT12!" | sudo tee /tmp/fatmount/hello.txt
#sudo umount /tmp/fatmount
#qemu-system-i386 -drive format=raw,file=./bin/os.bin
qemu-system-i386 \
  -drive format=raw,file=./bin/os.bin,if=ide,index=0 \
  -drive format=raw,file=./bin/disk.img,if=ide,index=1 \
  -d int,cpu_reset -D qemu.log