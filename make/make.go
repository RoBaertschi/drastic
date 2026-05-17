package main

import (
	"archive/tar"
	"compress/gzip"
	"context"
	"errors"
	"fmt"
	"net/http"
	"os"
)

func downloadGziped(ctx context.Context, c *http.Client, doneChan chan downloadResult, url string) {
	req, err := http.NewRequestWithContext(ctx, "GET", url, nil)
	if err != nil {
		doneChan <- downloadResult{err: err}
		return
	}

	res, err := c.Do(req)
	if err != nil {
		doneChan <- downloadResult{err: err}
		return
	}

	rGzip, err := gzip.NewReader(res.Body)
	if err != nil {
		doneChan <- downloadResult{err: err}
		return
	}

	rTar := tar.NewReader(rGzip)
	doneChan <- downloadResult{download: download{
		r:    rTar,
		gzip: rGzip,
		req:  req,
	}}
}

type download struct {
	r    *tar.Reader
	gzip *gzip.Reader
	req  *http.Request
}

type downloadResult struct {
	err      error
	download download
}

func downloadManyGzipedTars(ctx context.Context, urls []string) (downloads []download, err error) {
	var c http.Client
	doneChan := make(chan downloadResult, 5)
	doneUrls := 0
	errs := []error{}

	// defer ctxCancel()

	for _, url := range urls {
		go downloadGziped(ctx, &c, doneChan, url)
	}

wait:
	for doneUrls < len(urls) {
		select {
		case result := <-doneChan:
			doneUrls += 1
			if result.err != nil {
				errs = append(errs, result.err)
			} else {
				downloads = append(downloads, result.download)
			}
		case <-ctx.Done():
			break wait
		}
	}

	return downloads, errors.Join(errs...)
}

func main() {
	ctx := context.Background()
	downloads, err := downloadManyGzipedTars(ctx, []string{"https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz"})
	fmt.Println("downloads done, unzipping")

	for _, d := range downloads {
		var err error
		for err == nil {
			var header *tar.Header
			header, err = d.r.Next()
			if err != nil {
				fmt.Println("err", err)
				continue
			}

			data := make([]byte, header.Size)
			fmt.Println(
				d.r.Read(data),
			)

			fmt.Println(header)
		}
	}

	fmt.Printf("%#v %#v\n", downloads, err)
}
