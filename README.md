# modreverse
A simple Linux kernel module.


## Compilation

    make
    gcc test.c -o test

## Example usage

    sudo insmod reverse.ko buffer_size=2048
    ./test "On a world supported on the back of a giant turtle.."
    ...
    sudo rmmod reverse

Fork, play.





