# Week 1 Notes

## 상담내용

- 문서보다는 코드를 위주로 보는 것이 낫다.
- 리눅스 커널은 요구사항이 너무 많으므로 (특히 디바이스 드라이버), 원하면 교수님께서 미니멀 커널을 제공해 주실것이다.
- 하지만 리눅스 커널보다는 **xv6**나 pintos를 사용 하는 것이 좋다.
- xv6는 박스(Bochs)로 실행할 수 있고, pintos는 qemu로 실행 가능하다. 다만, 꼭 정해져 있는 건 아니기에 바꿔서 써도 괜찮다.
- 우선 xv6을 박스로 컴파일해보자. 인터넷에 검색하면 방법이 잘 나올 것이다. 잘 된다면, 이제 내가 만든 VMM을 이용해 해보고 문제 없으면 되는거다.
- 중간/기말, 소프트콘 제출 등을 고려하면 실질적인 개발 주수는 12주 남짓이다. 따라서 시간이 날 때 달려야 한다.
- 연구노트 작성은 미리미리. 매주 작성하고 꼭 끝에 todo를 작성해서 다음주의 목표를 정하자.
- qemu 코드는 너무 크고, architecture independent이기 때문에 코드가 방대하다. 따라서 qemu보다는 Bochs의 코드를 참고하는 것이 가장 좋은 방법이다.
- 교수님께 질문/도움요청은 슬랙으로.
- 러스트도 공부해보자. 나중에 끝날때쯤 교수님께 러스트로 만들어진 커널을 받아서 돌리는 것도 목표 중 하나로 좋을듯.

## 연구

Main priorities: Reading docs.

### pre-meeting study: Reading QEMU code & KVM API docs.

#### [KVM API documentation](https://www.kernel.org/doc/html/latest/virt/kvm/api.html)

The KVM API is centered around **file descriptors**. ioctls are issued on these file descriptors.

#### open("/dev/kvm")

`open("/dev/kvm")` returns a handle for the KVM subsystem.

For reference, I checked out [QEMU code](https://gitlab.com/qemu-project/qemu.git):

```C
// accel/kvm/kvm-all.c
// line 2606~
static int kvm_init(AccelState *as, MachineState *ms)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    static const char upgrade_note[] =
        "Please upgrade to at least kernel 4.5.\n";
    const struct {
        const char *name;
        int num;
    } num_cpus[] = {
        { "SMP",          ms->smp.cpus },
        { "hotpluggable", ms->smp.max_cpus },
        { /* end of list */ }
    }, *nc = num_cpus;
    int soft_vcpus_limit, hard_vcpus_limit;
    KVMState *s = KVM_STATE(as);
    const KVMCapabilityInfo *missing_cap;
    int ret;
    int type;

    qemu_mutex_init(&kml_slots_lock);

    /*
     * On systems where the kernel can support different base page
     * sizes, host page size may be different from TARGET_PAGE_SIZE,
     * even with KVM.  TARGET_PAGE_SIZE is assumed to be the minimum
     * page size for the system though.
     */
    assert(TARGET_PAGE_SIZE <= qemu_real_host_page_size());

    s->sigmask_len = 8;
    accel_blocker_init();

#ifdef TARGET_KVM_HAVE_GUEST_DEBUG
    QTAILQ_INIT(&s->kvm_sw_breakpoints);
#endif
    QLIST_INIT(&s->kvm_parked_vcpus);
    s->fd = qemu_open_old(s->device ?: "/dev/kvm", O_RDWR);
    if (s->fd == -1) {
        error_report("Could not access KVM kernel module: %m");
        ret = -errno;
        goto err;
    }
```

It seems like the function `kvm_init` triggers `qemu_open_old`. So I checked that too:

```C
// util/osdep.c
// line 376~
int qemu_open_old(const char *name, int flags, ...)
{
    va_list ap;
    mode_t mode = 0;
    int ret;

    va_start(ap, flags);
    if (flags & O_CREAT) {
        mode = va_arg(ap, int);
    }
    va_end(ap);

    ret = qemu_open_internal(name, flags, mode, NULL);

#ifdef O_DIRECT
    if (ret == -1 && errno == EINVAL && (flags & O_DIRECT)) {
        error_report("file system may not support O_DIRECT");
        errno = EINVAL; /* in case it was clobbered */
    }
#endif /* O_DIRECT */

    return ret;
}
```

`"/dev/kvm"` is passed to `qemu_open_internal` as `flags`.

#### KVM_CREATE_VM

```C
// accel/kvm/kvm-all.c
// line 2506~2536
static int do_kvm_create_vm(KVMState *s, int type)
{
    int ret;

    do {
        ret = kvm_ioctl(s, KVM_CREATE_VM, type);
    } while (ret == -EINTR);

    if (ret < 0) {
        error_report("ioctl(KVM_CREATE_VM) failed: %s", strerror(-ret));

#ifdef TARGET_S390X
        if (ret == -EINVAL) {
            error_printf("Host kernel setup problem detected."
                         " Please verify:\n");
            error_printf("- for kernels supporting the"
                        " switch_amode or user_mode parameters, whether");
            error_printf(" user space is running in primary address space\n");
            error_printf("- for kernels supporting the vm.allocate_pgste"
                         " sysctl, whether it is enabled\n");
        }
#elif defined(TARGET_PPC)
        if (ret == -EINVAL) {
            error_printf("PPC KVM module is not loaded. Try modprobe kvm_%s.\n",
                         (type == 2) ? "pr" : "hv");
        }
#endif
    }

    return ret;
}
```
