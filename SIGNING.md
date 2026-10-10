# Assinatura e distribuicao

Esta versao NAO esta assinada. O icone, os metadados de versao e o hash nao sao uma certificacao de seguranca.

"Windows protegeu o computador" e um aviso do Microsoft Defender SmartScreen, que pode aparecer para aplicativos novos ou sem reputacao. Uma assinatura Authenticode confiavel identifica o autor e protege a integridade, mas nao garante ausencia de avisos, deteccoes ou bloqueios. Certificados autoassinados nao estabelecem reputacao publica. Certificados EV tambem nao garantem reputacao imediata.

Para distribuicao publica, obtenha uma identidade de assinatura validada por uma autoridade reconhecida ou pelo servico Microsoft Artifact Signing, se elegivel. Isso exige cadastro/validacao e pode ter custos; nada foi contratado, comprado ou configurado neste projeto. Outra opcao e distribuicao por MSIX na Microsoft Store, sujeita as regras e certificacao da Store.

## Fluxo de release

1. Compile em Release e execute os testes isolados.
2. Assine o executavel final com SHA-256 e carimbo de tempo. O script `tools/Sign-Release.ps1` atende certificados de assinatura de codigo instalados no armazenamento pessoal do Windows. Ele nao gera certificados nem exporta chaves. Servicos em nuvem exigem seu proprio cliente de assinatura.
3. Verifique a assinatura. Exemplo: `Get-AuthenticodeSignature .\ClickLimit.exe` deve retornar `Valid`.
4. Gere o ZIP e o hash somente APOS assinar. Qualquer modificacao posterior no executavel invalida a assinatura.
5. Publique com identidade consistente entre releases para construir reputacao. Nao anuncie que assinatura garante aprovacao do Windows.

Se houver deteccao de uma ameaca especifica pelo Defender, conserve o nome da deteccao e encaminhe o arquivo para analise da Microsoft. Isso e diferente do aviso de reputacao do SmartScreen. Nao e necessario desativar o antivirus ou criar exclusoes para desenvolver este projeto.

Fontes oficiais:
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation
- https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options
- https://www.microsoft.com/en-us/wdsi/filesubmission
