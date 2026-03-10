package boxbox

import (
	"context"
	"fmt"
	"io"
	"time"
)

func (s *Box) CloseWithTimeout(cancal context.CancelFunc, d time.Duration, logFunc func(v ...any), block bool) {
	start := time.Now()
	t := time.NewTimer(d)
	done := make(chan struct{})

	printCloseTime := func() {
		logFunc("[Info] sing-box closed in", fmt.Sprintf("%d ms", time.Since(start).Milliseconds()))
	}

	go func(cancel context.CancelFunc, closer io.Closer) {
		cancel()
		closer.Close()
		close(done)
		t.Stop()
	}(cancal, s)

	select {
	case <-t.C:
		logFunc("[Warning] sing-box close takes longer than expected")
		if block {
			select {
			case <-done:
				printCloseTime()
			}
		}
	case <-done:
		if !t.Stop() {
			<-t.C
		}
		printCloseTime()
	}
}
