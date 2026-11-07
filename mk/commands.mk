# Developer workflows share the same implementation as the launchers.
.PHONY: help run run-kernel debug panic doctor setup toolchain clean package vm-images create-disk prepare-disk
help:
	python3 scripts/rum.py --help

run run-kernel debug panic doctor setup toolchain clean package vm-images:
	python3 scripts/rum.py $@

create-disk prepare-disk:
	python3 scripts/rum.py $@
