package main

import (
	"fmt"
	"log"
	"os"

	"namecon/signal-svc/internal/core"
)

func main() {
	configPath := "configs/signal-svc.yaml"
	if len(os.Args) > 1 {
		configPath = os.Args[1]
	}

	server, err := core.New(configPath)
	if err != nil {
		fmt.Fprintf(os.Stderr, "Fatal: %v\n", err)
		os.Exit(1)
	}
	defer server.Close()

	log.Fatal(server.Run())
}
