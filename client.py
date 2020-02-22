#!/usr/bin/env python

# Swarthmore College, CS 43, Lab 4
# Copyright (c) 2019 Swarthmore College Computer Science Department,
# Swarthmore PA
# Professor Vasanta Chaganti

import ao
import mad
import readline
import socket
import struct
import sys
import threading
from time import sleep

# The Mad audio library we're using expects to be given a file object, but
# we're not dealing with files, we're reading audio data over the network.  We
# use this object to trick it.  All it really wants from the file object is the
# read() method, so we create this wrapper with a read() method for it to
# call, and it won't know the difference.
# You probably don't need to modify this.
class mywrapper(object):
    def __init__(self):
        self.mf = None
        self.data = ""

    # When it asks to read a specific size, give it that many bytes, and
    # update our remaining data.
    def read(self, size):
        result = self.data[:size]
        self.data = self.data[size:]
        return result

def recv_thread_func(wrap, cond_filled, sock):
    while True:
        #cond_filled.acquire()
        # TODO: You do NOT want to sleep here, or ANYWHERE.  This is only here
        # to keep the thread from using 100% of your CPU in a busy loop.
        total_bytes = 0
        while(total_bytes<2048):
            buffer = sock.recv(2048)
            total_bytes+=len(buffer)

        print("Buffer: ",buffer)

            #if(bytes_received<0):
                #perror("Recv")
            #total_bytes+=bytes_received

        type, = struct.unpack("b",buffer[0:1])
        print("TYPE: ", type)
        if(type == 0):
            print("We received some play data!")
            wrap.data = buffer[6:]
            # cond_filled.notify()
        if(type == 1):
            print("We received some info data!")
        if(type == 3):
            print("We received some list data!")
        # TODO: Receive messages.  If they're responses to info/list, print
        # the results for the user to see.  If they contain song data, the
        # data needs to be added to the wrapper object.  Be sure to protect
        # the wrapper with synchronization, since the other thread is using
        # it too!
        #cond_filled.release()


def play_thread_func(wrap, cond_filled, dev):
    while True:
        #cond_filled.acquire()
        # TODO: You do NOT want to sleep here, or ANYWHERE.  This is only here
        # to keep the thread from using 100% of your CPU in a busy loop.
        if (wrap.data != ""):
            wrap.mf = mad.MadFile(wrap)

            # Play the file.
            while True:
                buf = wrap.mf.read()
                if buf is None:  # eof
                    #cond_filled.release()
                    break
                dev.play(buffer(buf), len(buf))
                print("ran play()...")

            print("Played some stuff")

        # TODO: If there is song data stored in the wrapper object, play it!
        # Otherwise, wait until there is.  Be sure to protect your accesses
        # to the wrapper with synchronization, since the other thread is
        # using it too!

def send_helper(sockfd,request):
    total_sent = 0
    while(total_sent<6):
        sent_bytes = sockfd.send(request)
        if(sent_bytes == -1):
            perror("Send")
            return -1
        total_sent+= sent_bytes


def main():
    if len(sys.argv) < 3:
        print 'Usage: %s <server name/ip> <server port>' % sys.argv[0]
        sys.exit(1)

    # Create a pseudo-file wrapper, condition variable, and socket.  These will
    # be passed to the thread we're about to create.
    wrap = mywrapper()

    # Create a condition variable to synchronize the receiver and player threads.
    # In python, this implicitly creates a mutex lock too.
    # See: https://docs.python.org/2/library/threading.html#condition-objects
    cond_filled = threading.Condition()

    # Create a TCP socket and try connecting to the server.
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((sys.argv[1], int(sys.argv[2])))

    # Create a thread whose job is to receive messages from the server.
    recv_thread = threading.Thread(
        target=recv_thread_func,
        args=(wrap, cond_filled, sock)
    )
    recv_thread.daemon = True
    recv_thread.start()

    # Create a thread whose job is to play audio file data.
    dev = ao.AudioDevice('pulse')
    play_thread = threading.Thread(
        target=play_thread_func,
        args=(wrap, cond_filled, dev)
    )
    play_thread.daemon = True
    play_thread.start()

    # Enter our never-ending user I/O loop.  Because we imported the readline
    # module above, raw_input gives us nice shell-like behavior (up-arrow to
    # go backwards, etc.).
    # TODO: Send messages to the server when the user types things.
    while True:
        line = raw_input('>> ')

        if ' ' in line:
            cmd, args = line.split(' ', 1)
        else:
            cmd = line

        if cmd in ['l', 'list']:
            print 'The user asked for list.'
            request = struct.pack('h',2)
            request += struct.pack('i',13)
            send_helper(sock,request)



        if cmd in ['p', 'play']:
            print 'The user asked to play:', args
            print(int(args))
            request = struct.pack('h',0)
            print("Request:",request)
            request += struct.pack('i',int(args))
            print("Request:",request)

            send_helper(sock,request)

        if cmd in ['i', 'info']:
            print 'The user asked for info about:', args
            request = struct.pack('h',1)
            request += struct.pack('i',int(args))
            send_helper(sock,request)

        if cmd in ['s', 'stop']:
            print 'The user asked for stop.'
            request = struct.pack('h',3)
            request += struct.pack('i',13)
            send_helper(sock,request)


        if cmd in ['quit', 'q', 'exit']:
            sys.exit(0)

if __name__ == '__main__':
    main()
