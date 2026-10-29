export function loginErrorMessage(error) {
  if (error?.code === 'email_not_confirmed') return '邮箱尚未确认，请在 Supabase 后台确认该共享账号。';
  if (error?.code === 'over_request_rate_limit' || error?.status === 429) return '登录尝试过于频繁，请稍后再试。';
  if (error?.code === 'invalid_credentials' || error?.status === 400) return '登录失败：账号尚未创建或邮箱、密码不正确。请使用系统共享账号，不是直接使用邮箱服务商密码。';
  if (!error?.status || error.status >= 500) return '无法连接登录服务，请检查网络或稍后重试。';
  return '登录服务暂时不可用，请联系系统管理员。';
}
