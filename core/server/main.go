package main

import (
	"ThroneCore/gen"
	"ThroneCore/internal/boxmain"
	"context"
	"flag"
	"fmt"
	"github.com/xtls/xray-core/core"
	"google.golang.org/grpc"
	"log"
	"net"
	"os"
	"runtime"
	runtimeDebug "runtime/debug"
	"runtime/metrics"
	"strconv"
	"syscall"
	"time"

	_ "ThroneCore/internal/distro/all"
	C "github.com/sagernet/sing-box/constant"
)

func RunCore() {
	_port := flag.Int("port", 19810, "")
	_debug := flag.Bool("debug", false, "")
	_version := flag.Bool("version", false, "")
	flag.CommandLine.Parse(os.Args[1:])

	if *_version {
		fmt.Printf("sing-box: %s\n", C.Version)
		fmt.Printf("Xray-core: %s\n", core.Version())
		os.Exit(0)
	}

	debug = *_debug

	go func() {
		parent, err := os.FindProcess(os.Getppid())
		if err != nil {
			log.Fatalln("find parent:", err)
		}
		if runtime.GOOS == "windows" {
			state, err := parent.Wait()
			log.Fatalln("parent exited:", state, err)
		} else {
			for {
				time.Sleep(time.Second * 10)
				err = parent.Signal(syscall.Signal(0))
				if err != nil {
					log.Fatalln("parent exited:", err)
				}
			}
		}
	}()
	boxmain.DisableColor()

	// RPC
	addr := "127.0.0.1:" + strconv.Itoa(*_port)
	lis, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatalf("failed to listen: %v", err)
	}
	fmt.Printf("Core listening at %v\n", addr)

	s := grpc.NewServer(grpc.MaxRecvMsgSize(128 * 1024 * 1024))
	gen.RegisterLibcoreServiceServer(s, &server{})
	if err := s.Serve(lis); err != nil {
		log.Fatalf("failed to serve: %v", err)
	}
}

func main() {
	defer func() {
		if err := recover(); err != nil {
			fmt.Println("Core panicked:")
			fmt.Println(err)
			os.Exit(0)
		}
	}()
	fmt.Println("sing-box:", C.Version)
	fmt.Println("Xray-core:", core.Version())
	fmt.Println()
	runtimeDebug.SetMemoryLimit(2 * 1024 * 1024 * 1024) // 2GB
	go func() {
		sample := []metrics.Sample{{Name: "/memory/classes/heap/objects:bytes"}}
		for {
			time.Sleep(5 * time.Second)
			metrics.Read(sample)
			if sample[0].Value.Kind() == metrics.KindUint64 &&
				sample[0].Value.Uint64() > 1.5*1024*1024*1024 {
				// too much memory for sing-box, crash
				panic("Memory has reached 1.5 GB, this is not normal")
			}
		}
	}()

	testCtx, cancelTests = context.WithCancel(context.Background())
	RunCore()
	return
}
